#ifndef TRIGGER_SENSOR_H
#define TRIGGER_SENSOR_H

#include <stdint.h>

bool triggerSensorBegin();
void triggerSensorUpdate();
bool triggerSensorGetAngle(int16_t *angleTenthsOfDegree);

#endif