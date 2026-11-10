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
// BRKRL in mV, 0 = off (normal BRAKE).
uint16_t displayGetBrakeReleaseMilliVolts();
// TORQ: motor current at full trigger; false if off, BIPOL is on or MOTOR R is unknown.
bool displayGetTorque(uint32_t *maxMilliAmps, float *motorOhms);

struct BipolarSettings {
	float bandOhms;
	float rbOhms;
	float chokeOhms;
	float sensiOhms;
	float hfe;
	float motorOhms;
};
// False if BIPOL is off for the selected car or MOTOR R has not been measured.
bool displayGetBipolar(BipolarSettings *settings);
void displayGetPwmFrequencies(uint32_t *driveHz, uint32_t *brakeHz);
float expoCurve(float input, int16_t expo);
// CURVE: monotone cubic spline (Fritsch-Carlson) through (0,0), the point in % and (100,100).
struct CurvePoints {
	uint8_t x;
	uint8_t y;
};
float splineCurve(float input, const CurvePoints &points);
CurvePoints displayGetCurve();
DisplayMotorState displayGetMotorInputState();
void displaySetMotorState(DisplayMotorState state, uint16_t dutyPermille);

#endif