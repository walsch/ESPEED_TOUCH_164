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
	TriggerCalibrating,
};

void displayInit();
void displayUpdate();
void displaySensorUpdate();
bool displayTrackSupplyAvailable();
uint16_t displayTrackVoltageMilliVolts();
bool displayGetMotorControl(uint8_t *triggerPercent, uint8_t *sensiPercent, uint8_t *brakePercent);
int16_t displayGetExpo();
float expoCurve(float input, int16_t expo);
DisplayMotorState displayGetMotorInputState();
void displaySetMotorState(DisplayMotorState state, uint8_t dutyPercent);

#endif