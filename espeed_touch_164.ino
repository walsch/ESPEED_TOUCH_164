
#include "display_unit.h"
#include "trigger_sensor.h"
#include "btn9990_driver.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr uint32_t CONTROL_PERIOD_MS = 1;
constexpr uint32_t TRIGGER_SAMPLE_MS = CONTROL_PERIOD_MS;

void displayTask(void *parameter) {
	(void)parameter;
	for (;;) {
		displayUpdate();
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

void vehicleControlTask(void *parameter) {
	(void)parameter;
	bool sensorInitialized = false;
	bool sensorInitAttempted = false;
	uint32_t sensorInitAt = 0;
	uint32_t triggerSampleAt = 0;
	uint32_t temperatureSampleAt = 0;
	bool previousTrackSupplyAvailable = false;
	bool motorArmed = false;
	TickType_t lastWakeTime = xTaskGetTickCount();
	for (;;) {
		displaySensorUpdate();
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
			btn9990SetSupplyAvailable(trackSupplyAvailable, brakePercent);
			previousTrackSupplyAvailable = trackSupplyAvailable;
		}
		if (trackSupplyAvailable && now - temperatureSampleAt >= 1000) {
			temperatureSampleAt = now;
			btn9990SampleTemperature();
		}

		bool outputAccepted = true;
		uint8_t commandedDutyPercent = 0;
		if (!controlInputValid) {
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
			const uint8_t throttlePercent = sensiPercent +
				((100U - sensiPercent) * triggerPercent + 50U) / 100U;
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
		vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
	}
}
}

void setup() {
	btn9990Begin();
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
