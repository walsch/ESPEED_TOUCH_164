#ifndef TRIGGER_SENSOR_H
#define TRIGGER_SENSOR_H

#include <stdint.h>

// Reads the TLE493D every 1 ms in its own task on core 0.
bool triggerSensorStart();
bool triggerSensorReady();
// Increments with every read attempt; the control loop runs faster than the sensor.
uint32_t triggerSensorSampleCount();
bool triggerSensorGetAngle(int16_t *angleTenthsOfDegree);

#endif