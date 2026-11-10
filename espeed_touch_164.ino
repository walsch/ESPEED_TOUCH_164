
#include "display_unit.h"
#include "trigger_sensor.h"
#include "btn9990_driver.h"
#include "current_sensor.h"
#include "back_emf.h"
#include "motor_resistance.h"
#include "full_contact.h"
#include "adc_sampler.h"
#include <Arduino.h>
#include <driver/gptimer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t CONTROL_PERIOD_US = 500;
constexpr uint32_t DISPLAY_PERIOD_MS = 10;
// Reference: zero after VIN stable 200 ms and 1 s standstill, up to 5 tries 2 s apart.
constexpr uint32_t CURRENT_ZERO_VIN_STABLE_MS = 200;
constexpr uint32_t CURRENT_ZERO_STANDING_MS = 1000;
constexpr uint32_t CURRENT_ZERO_RETRY_MS = 2000;
constexpr uint8_t CURRENT_ZERO_TRIES = 5;
constexpr uint32_t BACK_EMF_SAMPLE_MS = 10;
// EMF observer for BIPOL: U = D * VIN - I * R_A every cycle, offset corrected by each BEMF sample.
constexpr float EMF_CORRECTION_GAIN = 0.3f;

uint16_t applyExpo(uint16_t permille, int16_t expo) {
	return static_cast<uint16_t>(expoCurve(permille / 1000.0f, expo) * 1000.0f + 0.5f);
}

// Ideal NPN emitter follower (constant hFE, no saturation) with motor feedback via SENSI:
// V_LIM - band(1-x) - R_B - node; node - CHOKE - base; node - SENSI - emitter = motor.
constexpr float BIPOLAR_VBE = 0.7f;

uint16_t bipolarDutyPermille(const BipolarSettings &b, uint16_t triggerPermille,
		float limitVolts, float vinVolts, float emfVolts) {
	const float x = triggerPermille / 1000.0f;
	const float rUp = b.bandOhms * (1.0f - x) + b.rbOhms;
	const float k = b.chokeOhms / b.sensiOhms;
	// Thevenin equivalent seen by the motor: U = U0 - R_out * I.
	const float rOut = (rUp * (1.0f + k) + b.chokeOhms) / (b.hfe + 1.0f + k);
	const float u0 = limitVolts - BIPOLAR_VBE * (1.0f + (rUp - rOut) / b.sensiOhms);
	if (vinVolts <= 0.0f || u0 <= emfVolts) {
		// An emitter follower cannot sink current: the motor coasts.
		return 0;
	}
	// Solved with the motor model I = (U - EMF) / R_A instead of a current loop.
	const float u = min((u0 * b.motorOhms + rOut * emfVolts) / (b.motorOhms + rOut), vinVolts);
	return static_cast<uint16_t>(u / vinVolts * 1000.0f + 0.5f);
}

// Reference wire_choke: AWG18 stranded copper, 7 mOhm per ft; current filter 10 ms.
constexpr uint32_t WIRE_MILLIOHM_PER_FOOT = 7;
constexpr uint32_t WIRE_CURRENT_TAU_US = 10000;

struct WireChoke {
	uint32_t lastUs = 0;
	int32_t filteredMilliAmps256 = 0;
	// Sub-step remainder in 1e-5 of full scale, carried to the next cycle.
	int32_t rest = 0;
};

// Series resistor in the track lead: motor voltage drops by I_motor * R, i.e. duty by I_motor * R / VIN.
uint16_t applyWireChoke(WireChoke &wire, uint16_t dutyPermille, uint16_t triggerPermille,
		int32_t motorMilliAmps, uint32_t vinMilliVolts, uint16_t wireTenthFeet, uint8_t limitPercent) {
	const uint32_t now = micros();
	const uint32_t dt = wire.lastUs == 0 ? WIRE_CURRENT_TAU_US : now - wire.lastUs;
	wire.lastUs = now;
	const int32_t current256 = max<int32_t>(0, motorMilliAmps) * 256;
	if (dt >= WIRE_CURRENT_TAU_US) {
		wire.filteredMilliAmps256 = current256;
	} else {
		wire.filteredMilliAmps256 += (int32_t)(((int64_t)(current256 - wire.filteredMilliAmps256) * (int32_t)dt) /
			WIRE_CURRENT_TAU_US);
	}
	if (wireTenthFeet == 0 || dutyPermille == 0 || vinMilliVolts == 0 ||
		triggerPermille > limitPercent * 10U) {
		wire.rest = 0;
		return dutyPermille;
	}
	const int64_t milliAmps = (wire.filteredMilliAmps256 + 128) / 256;
	const int32_t choke = (int32_t)((milliAmps * wireTenthFeet * WIRE_MILLIOHM_PER_FOOT * 10 + vinMilliVolts / 2) /
		vinMilliVolts);
	const int32_t out = (int32_t)dutyPermille * 100 - choke + wire.rest;
	if (out <= 0) {
		wire.rest = 0;
		return 0;
	}
	const int32_t quotient = out / 100;
	wire.rest = out - quotient * 100;
	return min<int32_t>(quotient, dutyPermille);
}

void displayTask(void *parameter) {
	(void)parameter;
	TickType_t lastWakeTime = xTaskGetTickCount();
	for (;;) {
		displayUpdate();
		if (xTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(DISPLAY_PERIOD_MS)) == pdFALSE) {
			// Overrun (e.g. page change): yield so the idle task still runs.
			vTaskDelay(1);
			lastWakeTime = xTaskGetTickCount();
		}
	}
}

bool IRAM_ATTR onControlTick(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *task) {
	BaseType_t woken = pdFALSE;
	vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(task), &woken);
	return woken == pdTRUE;
}

// The FreeRTOS tick (1 ms) is too coarse for the control period.
bool startControlTimer(TaskHandle_t task) {
	gptimer_handle_t timer = nullptr;
	const gptimer_config_t config = {
		.clk_src = GPTIMER_CLK_SRC_DEFAULT,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = 1000000,
	};
	gptimer_alarm_config_t alarm = {};
	alarm.alarm_count = CONTROL_PERIOD_US;
	alarm.flags.auto_reload_on_alarm = true;
	const gptimer_event_callbacks_t callbacks = {.on_alarm = onControlTick};
	return gptimer_new_timer(&config, &timer) == ESP_OK &&
		gptimer_set_alarm_action(timer, &alarm) == ESP_OK &&
		gptimer_register_event_callbacks(timer, &callbacks, task) == ESP_OK &&
		gptimer_enable(timer) == ESP_OK &&
		gptimer_start(timer) == ESP_OK;
}

void vehicleControlTask(void *parameter) {
	(void)parameter;
	bool previousTrackSupplyAvailable = false;
	bool motorArmed = false;
	uint16_t previousTriggerPermille = 0;
	uint32_t previousTriggerSample = 0;
	bool decelerating = false;
	WireChoke wireChoke;
	bool driving = false;
	uint32_t standingSince = 0;
	uint32_t vinPresentSince = 0;
	bool currentZeroPending = false;
	uint8_t currentZeroTries = 0;
	uint32_t currentZeroTriedAt = 0;
	uint32_t backEmfSampleAt = 0;
	uint16_t lastDrivePermille = 0;
	float bipolarMotorOhms = 0.0f;
	float emfCorrectionVolts = 0.0f;
	const bool controlTimerRunning = startControlTimer(xTaskGetCurrentTaskHandle());
	for (;;) {
		displaySensorUpdate();
		currentSensorUpdate();
		const uint32_t now = millis();
		const bool trackSupplyAvailable = displayTrackSupplyAvailable();
		const bool sensorInitialized = triggerSensorReady();

		uint16_t triggerPermille = 0;
		uint8_t sensiPercent = 0;
		uint8_t brakePercent = 100;
		const bool controlInputValid = sensorInitialized &&
			displayGetMotorControl(&triggerPermille, &sensiPercent, &brakePercent);
		const uint32_t triggerSample = triggerSensorSampleCount();
		const bool newTriggerSample = triggerSample != previousTriggerSample;
		DisplayMotorState motorState = DisplayMotorState::Initializing;
		if (!btn9990IsReady()) {
			motorState = DisplayMotorState::DriverError;
		} else if (!sensorInitialized) {
			motorState = DisplayMotorState::NoSensor;
		} else if (!controlInputValid) {
			motorState = displayGetMotorInputState();
		} else if (triggerPermille == 0) {
			motorState = trackSupplyAvailable ? DisplayMotorState::Braking : DisplayMotorState::NoVin;
		} else if (!motorArmed) {
			motorState = trackSupplyAvailable ? DisplayMotorState::LeaveBrake : DisplayMotorState::NoVin;
		} else if (!trackSupplyAvailable) {
			motorState = DisplayMotorState::DrivingLowVin;
		} else {
			motorState = DisplayMotorState::Driving;
		}
		if (trackSupplyAvailable != previousTrackSupplyAvailable) {
			if (trackSupplyAvailable) {
				vinPresentSince = now;
				currentZeroPending = true;
				currentZeroTries = 0;
			}
			if (trackSupplyAvailable || fullContactLowSideAllowed()) {
				btn9990SetSupplyAvailable(trackSupplyAvailable, brakePercent);
			}
			previousTrackSupplyAvailable = trackSupplyAvailable;
		}

		bool outputAccepted = true;
		uint32_t driveFrequencyHz;
		uint32_t brakeFrequencyHz;
		displayGetPwmFrequencies(&driveFrequencyHz, &brakeFrequencyHz);
		btn9990SetFrequencies(driveFrequencyHz, brakeFrequencyHz);
		uint16_t commandedDutyPermille = 0;
		uint8_t testDutyPercent = 0;
		const uint8_t bypassMode = displayGetBypassMode();
		const bool fullContactEnabled = bypassMode == 2;
		const uint32_t vinMilliVolts = displayTrackVoltageMilliVolts();
		// The contact must be open before the bridge brakes.
		auto openFullContact = [&]() { fullContactUpdate(fullContactEnabled, false, false, vinMilliVolts); };
		// Bridge off (motor coasts) until the contact has had time to open.
		auto applyBrake = [&]() -> bool {
			if (!fullContactLowSideAllowed()) {
				btn9990Disable();
				return true;
			}
			return btn9990SetBrakeDuty(brakePercent);
		};
		if (motorResistanceUpdate(trackSupplyAvailable, vinMilliVolts, &testDutyPercent)) {
			openFullContact();
			motorArmed = false;
			motorState = DisplayMotorState::Driving;
			commandedDutyPermille = testDutyPercent * 10U;
		} else if (!controlInputValid) {
			openFullContact();
			motorArmed = false;
			outputAccepted = applyBrake();
			commandedDutyPermille = brakePercent * 10U;
		} else if (triggerPermille == 0) {
			openFullContact();
			motorArmed = true;
			outputAccepted = applyBrake();
			commandedDutyPermille = brakePercent * 10U;
		} else if (!motorArmed) {
			openFullContact();
			outputAccepted = applyBrake();
			commandedDutyPermille = brakePercent * 10U;
		} else {
			const uint16_t curvedPermille = applyExpo(triggerPermille, displayGetExpo());
			const uint16_t sensiPermille = sensiPercent * 10U;
			uint16_t curvedThrottlePermille = sensiPermille +
				((1000U - sensiPermille) * curvedPermille + 500U) / 1000U;
			const bool triggerFull = triggerPermille >= 1000;
			const uint32_t limitMilliVolts = displayGetVoltageLimitMilliVolts();
			BipolarSettings bipolar;
			const bool bipolarActive = displayGetBipolar(&bipolar);
			if (bipolarActive) {
				bipolarMotorOhms = bipolar.motorOhms;
				float emfVolts = 0.0f;
				int32_t averageMilliAmps = 0;
				uint16_t emfMilliVolts = 0;
				if (lastDrivePermille > 0 && currentSensorGetMilliAmps(&averageMilliAmps)) {
					emfVolts = max(0.0f, lastDrivePermille / 1000.0f * vinMilliVolts / 1000.0f -
						averageMilliAmps / 1000.0f * bipolar.motorOhms + emfCorrectionVolts);
				} else if (backEmfGetMilliVolts(&emfMilliVolts)) {
					emfVolts = emfMilliVolts / 1000.0f;
				}
				const uint32_t vLimit = limitMilliVolts > 0 && limitMilliVolts < vinMilliVolts
					? limitMilliVolts : vinMilliVolts;
				curvedThrottlePermille = trackSupplyAvailable
					? bipolarDutyPermille(bipolar, triggerPermille, vLimit / 1000.0f,
						vinMilliVolts / 1000.0f, emfVolts)
					: 0;
			} else {
				bipolarMotorOhms = 0.0f;
			}
			if (bypassMode > 0 && triggerFull) {
				// BYPAS: everything available at full trigger (no curve, no LIMIT; FULL mode adds the contact).
				curvedThrottlePermille = 1000;
			} else if (!bipolarActive) {
				// LIMIT (reference target voltage): mean drive voltage at most LIMIT.
				if (limitMilliVolts > 0) {
					if (!trackSupplyAvailable) {
						curvedThrottlePermille = 0;
					} else if (limitMilliVolts < vinMilliVolts) {
						curvedThrottlePermille = (curvedThrottlePermille * limitMilliVolts + vinMilliVolts / 2) / vinMilliVolts;
					}
				}
			}
			int32_t motorMilliAmps = 0;
			currentSensorGetMilliAmps(&motorMilliAmps);
			uint16_t wireTenthFeet;
			uint8_t wireLimitPercent;
			displayGetWire(&wireTenthFeet, &wireLimitPercent);
			const uint16_t throttlePermille = applyWireChoke(wireChoke, curvedThrottlePermille, triggerPermille,
				motorMilliAmps, vinMilliVolts, wireTenthFeet, wireLimitPercent);
			// Reference DRAGM DECL: drag only while the trigger is being released.
			// Judged per trigger sample, the loop runs faster than the sensor.
			if (triggerPermille >= 1000) {
				decelerating = false;
			} else if (newTriggerSample) {
				decelerating = triggerPermille < previousTriggerPermille;
			}
			fullContactUpdate(fullContactEnabled, triggerFull, throttlePermille >= 1000, vinMilliVolts);
			const uint16_t dragPermille = (decelerating || displayGetDragContinuous()) && fullContactLowSideAllowed()
				? displayGetDragBrake() * 10U : 0;
			commandedDutyPermille = throttlePermille;
			if (!btn9990Enable() || !btn9990SetOutput(throttlePermille, dragPermille)) {
				openFullContact();
				outputAccepted = applyBrake();
				commandedDutyPermille = brakePercent * 10U;
			} else if (fullContactEngaged()) {
				motorState = DisplayMotorState::DrivingFull;
			}
		}
		if (!controlInputValid || newTriggerSample) {
			previousTriggerPermille = controlInputValid ? triggerPermille : 0;
			previousTriggerSample = triggerSample;
		}
		lastDrivePermille = btn9990IsOutputEnabled() ? commandedDutyPermille : 0;
		if (!btn9990IsOutputEnabled()) {
			decelerating = false;
		}
		if (!outputAccepted) {
			motorState = DisplayMotorState::DriverError;
		}
		displaySetMotorState(motorState, commandedDutyPermille);

		const bool wasDriving = driving;
		driving = btn9990IsOutputEnabled();
		if (driving || wasDriving) {
			standingSince = now;
		}
		if (now - backEmfSampleAt >= BACK_EMF_SAMPLE_MS && !fullContactRequested()) {
			backEmfSampleAt = now;
			int32_t milliAmpsBefore = 0;
			currentSensorGetLatestMilliAmps(&milliAmpsBefore);
			btn9990PauseOutput();
			backEmfSample(!driving, milliAmpsBefore, vinMilliVolts);
			btn9990ResumeOutput();
			uint16_t measuredMilliVolts;
			int32_t averageMilliAmps;
			if (bipolarMotorOhms > 0.0f && lastDrivePermille > 0 &&
				backEmfGetMilliVolts(&measuredMilliVolts) && currentSensorGetMilliAmps(&averageMilliAmps)) {
				const float modelVolts = lastDrivePermille / 1000.0f * vinMilliVolts / 1000.0f -
					averageMilliAmps / 1000.0f * bipolarMotorOhms;
				emfCorrectionVolts += EMF_CORRECTION_GAIN *
					(measuredMilliVolts / 1000.0f - modelVolts - emfCorrectionVolts);
			}
		}
		const bool manualZero = currentSensorTakeZeroRequest();
		const bool autoZero = currentZeroPending && trackSupplyAvailable &&
			now - vinPresentSince >= CURRENT_ZERO_VIN_STABLE_MS &&
			now - standingSince >= CURRENT_ZERO_STANDING_MS &&
			(currentZeroTries == 0 || now - currentZeroTriedAt >= CURRENT_ZERO_RETRY_MS);
		if (manualZero && driving) {
			currentSensorRejectZero();
		} else if (manualZero || autoZero) {
			// Bridge off so no current flows; the next cycle restores the brake.
			btn9990Disable();
			vTaskDelay(pdMS_TO_TICKS(5));
			const bool zeroOk = currentSensorMeasureOffset();
			if (autoZero) {
				currentZeroTries++;
				currentZeroTriedAt = now;
			}
			if (zeroOk || currentZeroTries >= CURRENT_ZERO_TRIES) {
				currentZeroPending = false;
			}
			// Drop the ticks missed meanwhile.
			ulTaskNotifyTake(pdTRUE, 0);
		}
		if (!controlTimerRunning) {
			vTaskDelay(1);
		} else {
			// Overruns collapse into one immediate cycle instead of a burst.
			ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		}
	}
}
}

void setup() {
	adcSamplerBegin();
	// First DMA frames arrive after 100 us; give all inputs a few.
	delay(2);
	btn9990Begin();
	fullContactBegin();
	currentSensorBegin();
	backEmfBegin();
	triggerSensorStart();
	displayInit();
	uint16_t triggerPermille = 0;
	uint8_t sensiPercent = 0;
	uint8_t brakePercent = 100;
	displayGetMotorControl(&triggerPermille, &sensiPercent, &brakePercent);
	btn9990SetSupplyAvailable(false, brakePercent);
	xTaskCreatePinnedToCore(displayTask, "Display", 8192, nullptr, 1, nullptr, 0);
	xTaskCreatePinnedToCore(vehicleControlTask, "VehicleControl", 4096, nullptr, 2, nullptr, 1);
}

void loop() {
	vTaskDelay(portMAX_DELAY);
}
