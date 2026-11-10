#include "motor_resistance.h"

#include <Arduino.h>
#include "btn9990_driver.h"
#include "current_sensor.h"
#include "back_emf.h"

namespace {
constexpr uint32_t SETTLE_MS = 100;
constexpr uint32_t AVERAGE_MS = 200;
constexpr int32_t MIN_DELTA_MILLIAMPS = 200;
constexpr int32_t ABORT_MILLIAMPS = 20000;
constexpr uint16_t TURNING_BACK_EMF_MV = 500;
// Freewheel diode drop seen by the motor during the off phase.
constexpr uint32_t DIODE_DROP_MV = 700;

uint8_t requestedDuty = 0;
bool requested = false;
MotorResistanceState state = MotorResistanceState::Idle;
uint32_t resultMilliOhms = 0;
int32_t resultLowMilliAmps = 0;
int32_t resultHighMilliAmps = 0;

uint8_t highDuty = 0;
uint8_t phase = 0;
uint32_t phaseStartMs = 0;
int64_t currentSum = 0;
uint64_t vinSum = 0;
uint32_t sampleCount = 0;
int32_t phaseMilliAmps[2] = {};
uint32_t phaseVin[2] = {};

void finish(MotorResistanceState result) {
  btn9990SetBrakeDuty(100);
  __atomic_store_n(&state, result, __ATOMIC_RELEASE);
}
}

void motorResistanceRequest(uint8_t dutyPercent) {
  __atomic_store_n(&requestedDuty, dutyPercent, __ATOMIC_RELAXED);
  __atomic_store_n(&state, MotorResistanceState::Running, __ATOMIC_RELEASE);
  __atomic_store_n(&requested, true, __ATOMIC_RELEASE);
}

MotorResistanceState motorResistanceGetState() {
  return __atomic_load_n(&state, __ATOMIC_ACQUIRE);
}

bool motorResistanceGetResult(uint32_t *milliOhms, int32_t *lowMilliAmps, int32_t *highMilliAmps) {
  if (motorResistanceGetState() != MotorResistanceState::Done) {
    return false;
  }
  *milliOhms = resultMilliOhms;
  *lowMilliAmps = resultLowMilliAmps;
  *highMilliAmps = resultHighMilliAmps;
  return true;
}

bool motorResistanceUpdate(bool supplyAvailable, uint16_t vinMilliVolts, uint8_t *dutyPercent) {
  if (__atomic_exchange_n(&requested, false, __ATOMIC_ACQ_REL)) {
    highDuty = constrain(__atomic_load_n(&requestedDuty, __ATOMIC_RELAXED), 2, 100);
    phase = 0;
    phaseStartMs = millis();
    currentSum = 0;
    vinSum = 0;
    sampleCount = 0;
  } else if (motorResistanceGetState() != MotorResistanceState::Running) {
    return false;
  }

  if (!supplyAvailable) {
    finish(MotorResistanceState::Failed);
    return false;
  }

  const uint8_t duty = phase == 0 ? highDuty / 2 : highDuty;
  if (!btn9990Enable() || !btn9990SetOutput(duty, 0)) {
    finish(MotorResistanceState::Failed);
    return false;
  }
  *dutyPercent = duty;

  int32_t milliAmps = 0;
  if (!currentSensorGetMilliAmps(&milliAmps) || abs(milliAmps) > ABORT_MILLIAMPS) {
    finish(MotorResistanceState::Failed);
    return false;
  }
  const uint32_t elapsed = millis() - phaseStartMs;
  if (elapsed >= SETTLE_MS) {
    currentSum += milliAmps;
    vinSum += vinMilliVolts;
    sampleCount++;
  }
  if (elapsed < SETTLE_MS + AVERAGE_MS) {
    return true;
  }

  phaseMilliAmps[phase] = sampleCount ? (int32_t)(currentSum / sampleCount) : 0;
  phaseVin[phase] = sampleCount ? (uint32_t)(vinSum / sampleCount) : 0;
  if (phase == 0) {
    phase = 1;
    phaseStartMs = millis();
    currentSum = 0;
    vinSum = 0;
    sampleCount = 0;
    return true;
  }

  uint16_t backEmf = 0;
  if (backEmfGetMilliVolts(&backEmf) && backEmf > TURNING_BACK_EMF_MV) {
    finish(MotorResistanceState::Turning);
    return false;
  }
  const int32_t deltaMilliAmps = phaseMilliAmps[1] - phaseMilliAmps[0];
  if (deltaMilliAmps < MIN_DELTA_MILLIAMPS) {
    finish(MotorResistanceState::Failed);
    return false;
  }
  const uint32_t vin = (phaseVin[0] + phaseVin[1]) / 2;
  const uint32_t deltaDuty = highDuty - highDuty / 2;
  // dU = dDuty * (VIN + Vdiode): duty-independent losses cancel in the difference.
  resultMilliOhms = (uint32_t)((uint64_t)deltaDuty * (vin + DIODE_DROP_MV) * 10 / deltaMilliAmps);
  resultLowMilliAmps = phaseMilliAmps[0];
  resultHighMilliAmps = phaseMilliAmps[1];
  finish(MotorResistanceState::Done);
  return false;
}
