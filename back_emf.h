#pragma once

#include <stdint.h>

// Motor voltage on GPIO3 via 15k/2.2k (power board), measured with the bridge briefly off.
void backEmfBegin();
// Control task: call right after switching both bridge inputs off; waits 25 ... 300 us until
// the winding current has decayed. motorMilliAmps is the current before switching off.
void backEmfSample(bool standing, int32_t motorMilliAmps, uint32_t vinMilliVolts);
uint32_t backEmfLastSettleMicros();
bool backEmfGetMilliVolts(uint16_t *milliVolts);
bool backEmfOffsetLearned();
// Instantaneous motor terminal voltage without switching the bridge; false if 0 V.
bool backEmfReadMotorMilliVolts(uint32_t *milliVolts);
