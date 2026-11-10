#pragma once

#include <stdint.h>

// TLE4971 on the power board, read via U8 on GPIO1: 1.25 V at 0 A, 20 mV/A.
constexpr int32_t CURRENT_GAIN_MIN = 40000;
constexpr int32_t CURRENT_GAIN_MAX = 60000;

void currentSensorBegin();
// Control task only.
void currentSensorUpdate();
bool currentSensorMeasureOffset();
bool currentSensorTakeZeroRequest();
void currentSensorRejectZero();

// Any task.
bool currentSensorGetMilliAmps(int32_t *averageMilliAmps);
bool currentSensorGetRawMilliVolts(uint16_t *milliVolts);
uint16_t currentSensorGetOffsetMilliVolts();
bool currentSensorOffsetMeasured();
int32_t currentSensorGetGain();
void currentSensorRequestZero();
// 0 = none, 1 = ok, 2 = failed (last zero attempt).
uint8_t currentSensorZeroResult();
// Display task: scales the gain so the averaged current reads targetMilliAmps and stores it.
bool currentSensorCalibrateGain(int32_t targetMilliAmps);
