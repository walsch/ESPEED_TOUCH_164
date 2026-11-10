#include "current_sensor.h"

#include <Arduino.h>
#include <Preferences.h>
#include "adc_sampler.h"
#include "full_contact.h"

namespace {
constexpr uint16_t OFFSET_NOMINAL_MV = 1250;
constexpr uint16_t OFFSET_MIN_MV = 1150;
constexpr uint16_t OFFSET_MAX_MV = 1350;
constexpr int32_t GAIN_NOMINAL = 50000;
// 0 V means sensor missing or unpowered; real values never go below ~-60 A.
constexpr uint16_t INVALID_BELOW_MV = 30;
constexpr int32_t MIN_CALIBRATION_MILLIAMPS = 500;
constexpr char PREFS_NAMESPACE[] = "espeed";
constexpr char PREFS_GAIN_KEY[] = "iGain";

uint16_t offsetMilliVolts = OFFSET_NOMINAL_MV;
bool offsetMeasured = false;
int32_t gain = GAIN_NOMINAL;
uint16_t rawMilliVolts = 0;
int32_t averageMilliAmps = 0;
int32_t latestMilliAmps = 0;
bool sampleValid = false;
bool zeroRequested = false;
uint8_t zeroResult = 0;
}

void currentSensorBegin() {
  Preferences prefs;
  if (prefs.begin(PREFS_NAMESPACE, true)) {
    gain = constrain(prefs.getInt(PREFS_GAIN_KEY, GAIN_NOMINAL), CURRENT_GAIN_MIN, CURRENT_GAIN_MAX);
    prefs.end();
  }
}

void currentSensorUpdate() {
  uint16_t milliVolts = 0;
  adcSamplerRead(AdcInput::Current, &milliVolts);
  __atomic_store_n(&rawMilliVolts, milliVolts, __ATOMIC_RELAXED);
  if (milliVolts < INVALID_BELOW_MV) {
    __atomic_store_n(&sampleValid, false, __ATOMIC_RELEASE);
    return;
  }
  const int32_t deltaMilliVolts = (int32_t)milliVolts - __atomic_load_n(&offsetMilliVolts, __ATOMIC_RELAXED);
  const int32_t milliAmps = (int32_t)(((int64_t)deltaMilliVolts * __atomic_load_n(&gain, __ATOMIC_RELAXED)) / 1000);
  __atomic_store_n(&latestMilliAmps, milliAmps, __ATOMIC_RELAXED);
  // ~16 ms time constant at 0.5 ms cycle, as in the reference.
  int32_t average = __atomic_load_n(&averageMilliAmps, __ATOMIC_RELAXED);
  average += (milliAmps - average) / 32;
  __atomic_store_n(&averageMilliAmps, average, __ATOMIC_RELAXED);
  __atomic_store_n(&sampleValid, true, __ATOMIC_RELEASE);
}

bool currentSensorMeasureOffset() {
  uint32_t sum = 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < 24 && count < 16; i++) {
    // One new current sample every 50 us from the sampler.
    delayMicroseconds(100);
    uint16_t milliVolts = 0;
    adcSamplerRead(AdcInput::Current, &milliVolts);
    if (milliVolts >= INVALID_BELOW_MV) {
      sum += milliVolts;
      count++;
    }
  }
  const uint16_t value = count > 0 ? (sum + count / 2U) / count : 0;
  const bool ok = count >= 16 && value >= OFFSET_MIN_MV && value <= OFFSET_MAX_MV;
  if (ok) {
    __atomic_store_n(&offsetMilliVolts, value, __ATOMIC_RELAXED);
    __atomic_store_n(&offsetMeasured, true, __ATOMIC_RELEASE);
    __atomic_store_n(&averageMilliAmps, 0, __ATOMIC_RELAXED);
  }
  __atomic_store_n(&zeroResult, (uint8_t)(ok ? 1 : 2), __ATOMIC_RELEASE);
  return ok;
}

bool currentSensorTakeZeroRequest() {
  return __atomic_exchange_n(&zeroRequested, false, __ATOMIC_ACQ_REL);
}

void currentSensorRejectZero() {
  __atomic_store_n(&zeroResult, (uint8_t)2, __ATOMIC_RELEASE);
}

bool currentSensorGetMilliAmps(int32_t *milliAmps) {
  if (!milliAmps || !__atomic_load_n(&sampleValid, __ATOMIC_ACQUIRE)) {
    return false;
  }
  *milliAmps = __atomic_load_n(&averageMilliAmps, __ATOMIC_RELAXED);
  return true;
}

bool currentSensorGetLatestMilliAmps(int32_t *milliAmps) {
  if (!milliAmps || !__atomic_load_n(&sampleValid, __ATOMIC_ACQUIRE)) {
    return false;
  }
  *milliAmps = __atomic_load_n(&latestMilliAmps, __ATOMIC_RELAXED);
  return true;
}

bool currentSensorGetRawMilliVolts(uint16_t *milliVolts) {
  if (!milliVolts) {
    return false;
  }
  *milliVolts = __atomic_load_n(&rawMilliVolts, __ATOMIC_RELAXED);
  return *milliVolts >= INVALID_BELOW_MV;
}

uint16_t currentSensorGetOffsetMilliVolts() {
  return __atomic_load_n(&offsetMilliVolts, __ATOMIC_RELAXED);
}

bool currentSensorOffsetMeasured() {
  return __atomic_load_n(&offsetMeasured, __ATOMIC_ACQUIRE);
}

int32_t currentSensorGetGain() {
  return __atomic_load_n(&gain, __ATOMIC_RELAXED);
}

void currentSensorRequestZero() {
  __atomic_store_n(&zeroResult, (uint8_t)0, __ATOMIC_RELEASE);
  __atomic_store_n(&zeroRequested, true, __ATOMIC_RELEASE);
}

uint8_t currentSensorZeroResult() {
  return __atomic_load_n(&zeroResult, __ATOMIC_ACQUIRE);
}

bool currentSensorCalibrateGain(int32_t targetMilliAmps) {
  int32_t measured;
  if (!currentSensorOffsetMeasured() || !currentSensorGetMilliAmps(&measured) ||
      abs(measured) < MIN_CALIBRATION_MILLIAMPS || abs(targetMilliAmps) < MIN_CALIBRATION_MILLIAMPS ||
      (measured < 0) != (targetMilliAmps < 0)) {
    return false;
  }
  const int64_t scaled = (int64_t)currentSensorGetGain() * targetMilliAmps / measured;
  const int32_t newGain = constrain(scaled, (int64_t)CURRENT_GAIN_MIN, (int64_t)CURRENT_GAIN_MAX);
  __atomic_store_n(&gain, newGain, __ATOMIC_RELAXED);
  FullContactWriteGuard guard;
  Preferences prefs;
  if (!prefs.begin(PREFS_NAMESPACE, false)) {
    return false;
  }
  const bool saved = prefs.putInt(PREFS_GAIN_KEY, newGain) == sizeof(int32_t);
  prefs.end();
  return saved;
}
