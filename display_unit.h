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
	DrivingFull,
};

void displayInit();
void displayUpdate();
void displaySensorUpdate();
bool displayTrackSupplyAvailable();
uint16_t displayTrackVoltageMilliVolts();
bool displayGetMotorControl(uint16_t *triggerPermille, uint8_t *sensiPercent, uint8_t *brakePercent);
int16_t displayGetExpo();
uint8_t displayGetDragBrake();
bool displayGetDragContinuous();
// WIRE in 0.1 ft, WLIM in % trigger.
void displayGetWire(uint16_t *tenthFeet, uint8_t *limitPercent);
// Max drive voltage (LIMIT) in mV, 0 = off.
uint16_t displayGetVoltageLimitMilliVolts();
// BYPAS at full trigger: 0 = off, 1 = BTN 100 %, 2 = BTN 100 % plus FULL contact.
uint8_t displayGetBypassMode();
void displayGetPwmFrequencies(uint32_t *driveHz, uint32_t *brakeHz);
float expoCurve(float input, int16_t expo);
DisplayMotorState displayGetMotorInputState();
void displaySetMotorState(DisplayMotorState state, uint16_t dutyPermille);

#endif