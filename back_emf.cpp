#include "back_emf.h"

#include <Arduino.h>
#include "adc_sampler.h"

namespace {
constexpr uint32_t DIVIDER_LOWER_OHMS = 2200;
constexpr uint32_t DIVIDER_UPPER_OHMS = 15000;
// Reference BemfZeroLearn: 500 ms uninterrupted standstill with < 300 mV spread.
constexpr uint32_t ZERO_LEARN_MS = 500;
constexpr uint32_t ZERO_GAP_MS = 20;
constexpr uint16_t ZERO_STABLE_MV = 300;
constexpr uint32_t MAX_SETTLE_US = 400;
// The sampler delivers 4 motor samples per 100 us frame; 8 guarantees a frame taken after the pause.
constexpr uint32_t SAMPLES_PER_FRAME = 4;
constexpr uint16_t STABLE_MV = 150;
constexpr uint16_t CLAMP_MARGIN_MV = 300;
constexpr int32_t CURRENT_FLOWING_MA = 300;

uint32_t lastSettleUs = 0;

uint16_t readMotorMilliVolts() {
  uint16_t pinMilliVolts = 0;
  adcSamplerRead(AdcInput::MotorVoltage, &pinMilliVolts);
  return min<uint32_t>((uint32_t)pinMilliVolts * (DIVIDER_LOWER_OHMS + DIVIDER_UPPER_OHMS) / DIVIDER_LOWER_OHMS,
                       UINT16_MAX);
}

// Busy-waits until the sampler has delivered `count` more motor samples; false on timeout.
bool waitMotorSamples(uint32_t fromSequence, uint32_t count, uint32_t startUs) {
  while (adcSamplerMotorSequence() - fromSequence < count) {
    if (micros() - startUs >= MAX_SETTLE_US) {
      return false;
    }
  }
  return true;
}

uint16_t backEmfMilliVolts = 0;
bool backEmfValid = false;
int16_t offsetMilliVolts = 0;
bool offsetLearned = false;

uint32_t learnStartMs = 0;
uint32_t learnLastMs = 0;
uint32_t learnSum = 0;
uint32_t learnCount = 0;
uint16_t learnMin = 0;
uint16_t learnMax = 0;

void learnOffset(uint16_t milliVolts, uint32_t now) {
  const bool gap = learnStartMs == 0 || now - learnLastMs > ZERO_GAP_MS;
  learnLastMs = now;
  if (!gap) {
    learnMin = min(learnMin, milliVolts);
    learnMax = max(learnMax, milliVolts);
  }
  if (gap || learnMax - learnMin > ZERO_STABLE_MV) {
    learnStartMs = now ? now : 1;
    learnMin = learnMax = milliVolts;
    learnSum = 0;
    learnCount = 0;
  }
  learnSum += milliVolts;
  learnCount++;
  if (now - learnStartMs >= ZERO_LEARN_MS) {
    offsetMilliVolts = min<uint32_t>(learnSum / learnCount, INT16_MAX);
    __atomic_store_n(&offsetLearned, true, __ATOMIC_RELEASE);
    learnStartMs = 0;
  }
}
}

void backEmfBegin() {
}

void backEmfSample(bool standing, int32_t motorMilliAmps, uint32_t vinMilliVolts) {
  // While the winding current decays the output is clamped by a body diode (about 0 V when
  // driving, about VIN when braking); wait until it leaves the clamp and two frames agree.
  const uint32_t startUs = micros();
  uint32_t sequence = adcSamplerMotorSequence();
  waitMotorSamples(sequence, 2 * SAMPLES_PER_FRAME, startUs);
  uint16_t previous = readMotorMilliVolts();
  uint16_t raw = previous;
  sequence = adcSamplerMotorSequence();
  while (waitMotorSamples(sequence, SAMPLES_PER_FRAME, startUs)) {
    sequence = adcSamplerMotorSequence();
    raw = readMotorMilliVolts();
    const bool clampedLow = motorMilliAmps > CURRENT_FLOWING_MA && raw < CLAMP_MARGIN_MV;
    const bool clampedHigh = motorMilliAmps < -CURRENT_FLOWING_MA && raw + CLAMP_MARGIN_MV > vinMilliVolts;
    if (!clampedLow && !clampedHigh && abs((int32_t)raw - previous) <= STABLE_MV) {
      break;
    }
    previous = raw;
  }
  __atomic_store_n(&lastSettleUs, micros() - startUs, __ATOMIC_RELAXED);
  if (standing && !offsetLearned) {
    learnOffset(raw, millis());
  }
  const int32_t corrected = max<int32_t>(0, (int32_t)raw - offsetMilliVolts);
  __atomic_store_n(&backEmfMilliVolts, (uint16_t)corrected, __ATOMIC_RELAXED);
  __atomic_store_n(&backEmfValid, true, __ATOMIC_RELEASE);
}

bool backEmfGetMilliVolts(uint16_t *milliVolts) {
  if (!milliVolts || !__atomic_load_n(&backEmfValid, __ATOMIC_ACQUIRE)) {
    return false;
  }
  *milliVolts = __atomic_load_n(&backEmfMilliVolts, __ATOMIC_RELAXED);
  return true;
}

bool backEmfOffsetLearned() {
  return __atomic_load_n(&offsetLearned, __ATOMIC_ACQUIRE);
}

uint32_t backEmfLastSettleMicros() {
  return __atomic_load_n(&lastSettleUs, __ATOMIC_RELAXED);
}

bool backEmfReadMotorMilliVolts(uint32_t *milliVolts) {
  uint16_t pinMilliVolts = 0;
  if (!adcSamplerRead(AdcInput::MotorVoltage, &pinMilliVolts)) {
    return false;
  }
  *milliVolts = (uint32_t)pinMilliVolts * (DIVIDER_LOWER_OHMS + DIVIDER_UPPER_OHMS) / DIVIDER_LOWER_OHMS;
  return pinMilliVolts > 0;
}
