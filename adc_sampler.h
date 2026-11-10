#pragma once

#include <stdint.h>

// All analog inputs on ADC1, sampled continuously by DMA so readers never block.
enum class AdcInput : uint8_t {
  Current,       // GPIO1, TLE4971 via U8
  TrackVoltage,  // GPIO2, VIN divider
  MotorVoltage,  // GPIO3, motor/BEMF divider
  Supply,        // GPIO4, VCC divider
  Count,
};

bool adcSamplerBegin();
// Latest calibrated pin voltage; false until the first sample arrived.
bool adcSamplerRead(AdcInput input, uint16_t *milliVolts);
// Increments with every motor-voltage sample; used to wait for samples taken after an event.
uint32_t adcSamplerMotorSequence();
