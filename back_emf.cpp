#include "back_emf.h"

#include <Arduino.h>

namespace {
constexpr uint8_t MOTOR_VOLTAGE_PIN = 3;
constexpr uint32_t DIVIDER_LOWER_OHMS = 2200;
constexpr uint32_t DIVIDER_UPPER_OHMS = 15000;
// Reference BemfZeroLearn: 500 ms uninterrupted standstill with < 300 mV spread.
constexpr uint32_t ZERO_LEARN_MS = 500;
constexpr uint32_t ZERO_GAP_MS = 20;
constexpr uint16_t ZERO_STABLE_MV = 300;

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
  pinMode(MOTOR_VOLTAGE_PIN, INPUT);
  analogSetPinAttenuation(MOTOR_VOLTAGE_PIN, ADC_11db);
}

void backEmfSample(bool standing) {
  const uint32_t pinMilliVolts = analogReadMilliVolts(MOTOR_VOLTAGE_PIN);
  const uint16_t raw = min<uint32_t>(pinMilliVolts * (DIVIDER_LOWER_OHMS + DIVIDER_UPPER_OHMS) /
                                         DIVIDER_LOWER_OHMS, UINT16_MAX);
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

bool backEmfReadMotorMilliVolts(uint32_t *milliVolts) {
  const uint32_t pinMilliVolts = analogReadMilliVolts(MOTOR_VOLTAGE_PIN);
  *milliVolts = pinMilliVolts * (DIVIDER_LOWER_OHMS + DIVIDER_UPPER_OHMS) / DIVIDER_LOWER_OHMS;
  return pinMilliVolts > 0;
}
