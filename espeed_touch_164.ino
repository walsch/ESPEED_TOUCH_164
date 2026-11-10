
#include "display_unit.h"
#include "trigger_sensor.h"
#include "btn9990_driver.h"
#include "current_sensor.h"
#include "back_emf.h"
#include "motor_resistance.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t CONTROL_PERIOD_MS = 1;
constexpr uint32_t DISPLAY_PERIOD_MS = 10;
constexpr uint32_t TRIGGER_SAMPLE_MS = CONTROL_PERIOD_MS;
// Reference: zero after VIN stable 200 ms and 1 s standstill, up to 5 tries 2 s apart.
constexpr uint32_t CURRENT_ZERO_VIN_STABLE_MS = 200;
constexpr uint32_t CURRENT_ZERO_STANDING_MS = 1000;
constexpr uint32_t CURRENT_ZERO_RETRY_MS = 2000;
constexpr uint8_t CURRENT_ZERO_TRIES = 5;
constexpr uint32_t BACK_EMF_SAMPLE_MS = 10;
// Reference BEMF_SETTLE_US: wait after switching off before reading.
constexpr uint32_t BACK_EMF_SETTLE_US = 25;

uint8_t applyExpo(uint8_t percent, int16_t expo) {
	return static_cast<uint8_t>(expoCurve(percent / 100.0f, expo) * 100.0f + 0.5f);
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

void vehicleControlTask(void *parameter) {
	(void)parameter;
	bool sensorInitialized = false;
	bool sensorInitAttempted = false;
	uint32_t sensorInitAt = 0;
	uint32_t triggerSampleAt = 0;
	bool previousTrackSupplyAvailable = false;
	bool motorArmed = false;
	bool driving = false;
	uint32_t standingSince = 0;
	uint32_t vinPresentSince = 0;
	bool currentZeroPending = false;
	uint8_t currentZeroTries = 0;
	uint32_t currentZeroTriedAt = 0;
	uint32_t backEmfSampleAt = 0;
	TickType_t lastWakeTime = xTaskGetTickCount();
	for (;;) {
		displaySensorUpdate();
		currentSensorUpdate();
		const uint32_t now = millis();
		const bool trackSupplyAvailable = displayTrackSupplyAvailable();
		if (!sensorInitialized && (!sensorInitAttempted || now - sensorInitAt >= 1000)) {
			sensorInitAttempted = true;
			sensorInitAt = now;
			sensorInitialized = triggerSensorBegin();
		}
		if (sensorInitialized && now - triggerSampleAt >= TRIGGER_SAMPLE_MS) {
			triggerSampleAt = now;
			triggerSensorUpdate();
		}

		uint8_t triggerPercent = 0;
		uint8_t sensiPercent = 0;
		uint8_t brakePercent = 100;
		const bool controlInputValid = sensorInitialized &&
			displayGetMotorControl(&triggerPercent, &sensiPercent, &brakePercent);
		DisplayMotorState motorState = DisplayMotorState::Initializing;
		if (!btn9990IsReady()) {
			motorState = DisplayMotorState::DriverError;
		} else if (!sensorInitialized) {
			motorState = DisplayMotorState::NoSensor;
		} else if (!controlInputValid) {
			motorState = displayGetMotorInputState();
		} else if (triggerPercent == 0) {
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
			btn9990SetSupplyAvailable(trackSupplyAvailable, brakePercent);
			previousTrackSupplyAvailable = trackSupplyAvailable;
		}

		bool outputAccepted = true;
		uint8_t commandedDutyPercent = 0;
		uint8_t testDutyPercent = 0;
		if (motorResistanceUpdate(trackSupplyAvailable, displayTrackVoltageMilliVolts(), &testDutyPercent)) {
			motorArmed = false;
			motorState = DisplayMotorState::Driving;
			commandedDutyPercent = testDutyPercent;
		} else if (!controlInputValid) {
			motorArmed = false;
			outputAccepted = btn9990SetBrakeDuty(brakePercent);
			commandedDutyPercent = brakePercent;
		} else if (triggerPercent == 0) {
			motorArmed = true;
			outputAccepted = btn9990SetBrakeDuty(brakePercent);
			commandedDutyPercent = brakePercent;
		} else if (!motorArmed) {
			outputAccepted = btn9990SetBrakeDuty(brakePercent);
			commandedDutyPercent = brakePercent;
		} else {
			const uint8_t curvedPercent = applyExpo(triggerPercent, displayGetExpo());
			const uint8_t throttlePercent = sensiPercent +
				((100U - sensiPercent) * curvedPercent + 50U) / 100U;
			commandedDutyPercent = throttlePercent;
			if (!btn9990Enable() || !btn9990SetOutput(throttlePercent, 0)) {
				outputAccepted = btn9990SetBrakeDuty(brakePercent);
				commandedDutyPercent = brakePercent;
			}
		}
		if (!outputAccepted) {
			motorState = DisplayMotorState::DriverError;
		}
		displaySetMotorState(motorState, commandedDutyPercent);

		const bool wasDriving = driving;
		driving = btn9990IsOutputEnabled();
		if (driving || wasDriving) {
			standingSince = now;
		}
		if (now - backEmfSampleAt >= BACK_EMF_SAMPLE_MS) {
			backEmfSampleAt = now;
			btn9990PauseOutput();
			delayMicroseconds(BACK_EMF_SETTLE_US);
			backEmfSample(!driving);
			btn9990ResumeOutput();
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
			lastWakeTime = xTaskGetTickCount();
		}
		vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
	}
}
}

void setup() {
	btn9990Begin();
	currentSensorBegin();
	backEmfBegin();
	displayInit();
	uint8_t triggerPercent = 0;
	uint8_t sensiPercent = 0;
	uint8_t brakePercent = 100;
	displayGetMotorControl(&triggerPercent, &sensiPercent, &brakePercent);
	btn9990SetSupplyAvailable(false, brakePercent);
	xTaskCreatePinnedToCore(displayTask, "Display", 8192, nullptr, 1, nullptr, 0);
	xTaskCreatePinnedToCore(vehicleControlTask, "VehicleControl", 4096, nullptr, 2, nullptr, 1);
}

void loop() {
	vTaskDelay(portMAX_DELAY);
}
