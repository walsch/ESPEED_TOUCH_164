#pragma once

#include <stdint.h>

// Motor voltage on GPIO3 via 15k/2.2k (power board), measured with the bridge briefly off.
void backEmfBegin();
// Control task: call while both switches are off; standing = output not driving.
void backEmfSample(bool standing);
bool backEmfGetMilliVolts(uint16_t *milliVolts);
bool backEmfOffsetLearned();
// Instantaneous motor terminal voltage without switching the bridge; false if 0 V.
bool backEmfReadMotorMilliVolts(uint32_t *milliVolts);
