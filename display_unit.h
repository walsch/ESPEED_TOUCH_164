#ifndef DISPLAY_UNIT_H
#define DISPLAY_UNIT_H

#include <stdint.h>

enum class DisplayMotorState : uint8_t {
	Initializing,
	DriverError,
	NoVin,
	NoSensor,
	CalibrationRequired,
	LeaveBrake,
	Braking,
	Driving,
	DrivingLowVin,
	NotRaceScreen,
};

void displayInit();
void displayUpdate();
void displaySensorUpdate();
bool displayTrackSupplyAvailable();
bool displayGetMotorControl(uint8_t *triggerPercent, uint8_t *sensiPercent, uint8_t *brakePercent);
DisplayMotorState displayGetMotorInputState();
void displaySetMotorState(DisplayMotorState state, uint8_t dutyPercent);

#endif