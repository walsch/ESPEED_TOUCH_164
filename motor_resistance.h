#pragma once

#include <stdint.h>

// Stalled-armature resistance test: two duties d/2 and d, R = dU / dI.
// The two-point difference cancels switching delay, diode drop and current offset.
enum class MotorResistanceState : uint8_t { Idle, Running, Done, Failed, Turning };

// Display task.
void motorResistanceRequest(uint8_t dutyPercent);
MotorResistanceState motorResistanceGetState();
bool motorResistanceGetResult(uint32_t *milliOhms, int32_t *lowMilliAmps, int32_t *highMilliAmps);

// Control task, every cycle. Returns true while it owns the output; *dutyPercent is the applied duty.
bool motorResistanceUpdate(bool supplyAvailable, uint16_t vinMilliVolts, uint8_t *dutyPercent);
