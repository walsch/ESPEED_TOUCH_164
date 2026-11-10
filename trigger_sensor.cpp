#include "trigger_sensor.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

namespace {
constexpr int8_t TLE_SDA = 8;
constexpr int8_t TLE_SCL = 15;
constexpr uint32_t TLE_I2C_FREQUENCY = 1000000;
constexpr uint8_t TLE_ADDRESS_A0 = 0x35;
constexpr uint8_t TLE_ADDRESS_P3 = 0x5D;
constexpr int16_t TLE_FIELD_MIN = 32;
constexpr uint32_t TLE_INVALID_HOLD_MS = 40;
constexpr float HALF_TURN_TENTHS_DEGREES = 1800.0f;
constexpr float RADIANS_PER_HALF_TURN = 3.14159265f;

enum class SensorVariant : uint8_t {
  Unknown,
  A0,
  P3,
};

SensorVariant sensorVariant = SensorVariant::Unknown;
uint8_t sensorAddress = 0;
bool sensorBusStarted = false;
bool sensorReady = false;
int16_t latestAngle = -1;
bool latestAngleValid = false;
uint32_t latestValidAt = 0;

bool writeBytes(uint8_t address, const uint8_t *bytes, uint8_t length) {
  Wire1.beginTransmission(address);
  for (uint8_t index = 0; index < length; index++) {
    Wire1.write(bytes[index]);
  }
  return Wire1.endTransmission() == 0;
}

bool probeAddress(uint8_t address) {
  Wire1.beginTransmission(address);
  return Wire1.endTransmission() == 0;
}

bool configureSensor() {
  if (sensorVariant == SensorVariant::P3) {
    const uint8_t configuration[] = {0x0A, 0xC6, 0x02};
    return writeBytes(sensorAddress, configuration, sizeof(configuration));
  }

  const uint8_t resetSequence[] = {0xFF, 0xFF, 0x00, 0x00};
  for (uint8_t command : resetSequence) {
    if (!writeBytes(sensorAddress, &command, 1)) {
      return false;
    }
  }
  delayMicroseconds(30);

  const uint8_t configuration1[] = {0x11, 0b00011001};
  const uint8_t configuration2[] = {0x10, 0b11111000};
  return writeBytes(sensorAddress, configuration1, sizeof(configuration1)) &&
         writeBytes(sensorAddress, configuration2, sizeof(configuration2));
}

int16_t angleFromXY(int16_t x, int16_t y) {
  const int32_t fieldSquared = static_cast<int32_t>(x) * x + static_cast<int32_t>(y) * y;
  if (fieldSquared < static_cast<int32_t>(TLE_FIELD_MIN) * TLE_FIELD_MIN) {
    return -1;
  }

  int16_t angle = static_cast<int16_t>(lroundf(
      atan2f(static_cast<float>(y), static_cast<float>(x)) *
      (HALF_TURN_TENTHS_DEGREES / RADIANS_PER_HALF_TURN)));
  if (angle < 0) {
    angle += 3600;
  }
  return angle;
}

bool readAngle(int16_t *angle) {
  uint8_t data[6];
  const bool p3 = sensorVariant == SensorVariant::P3;
  const uint8_t count = p3 ? 4 : 6;
  if (Wire1.requestFrom(sensorAddress, count) != count) {
    return false;
  }
  for (uint8_t index = 0; index < count; index++) {
    data[index] = Wire1.read();
  }

  int16_t x;
  int16_t y;
  if (p3) {
    x = static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) |
                             ((data[1] & 0x3F) << 2)) >> 2;
    y = static_cast<int16_t>((static_cast<uint16_t>(data[2]) << 8) |
                             ((data[3] & 0x3F) << 2)) >> 2;
  } else {
    x = static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) |
                             (data[4] & 0xF0)) >> 4;
    y = static_cast<int16_t>((static_cast<uint16_t>(data[1]) << 8) |
                             ((data[4] & 0x0F) << 4)) >> 4;
  }

  *angle = angleFromXY(x, y);
  return *angle >= 0;
}
}

bool triggerSensorBegin() {
  if (sensorReady) {
    return true;
  }
  if (!sensorBusStarted) {
    if (!Wire1.begin(TLE_SDA, TLE_SCL, TLE_I2C_FREQUENCY)) {
      return false;
    }
    Wire1.setTimeOut(2);
    sensorBusStarted = true;
  }

  const bool hasA0 = probeAddress(TLE_ADDRESS_A0);
  const bool hasP3 = probeAddress(TLE_ADDRESS_P3);
  if (hasA0 == hasP3) {
    return false;
  }

  sensorVariant = hasA0 ? SensorVariant::A0 : SensorVariant::P3;
  sensorAddress = hasA0 ? TLE_ADDRESS_A0 : TLE_ADDRESS_P3;
  sensorReady = configureSensor();
  return sensorReady;
}

void triggerSensorUpdate() {
  int16_t angle = -1;
  const bool valid = sensorReady && readAngle(&angle);
  if (valid) {
    __atomic_store_n(&latestAngle, angle, __ATOMIC_RELAXED);
    latestValidAt = millis();
    __atomic_store_n(&latestAngleValid, true, __ATOMIC_RELEASE);
  } else if (latestValidAt == 0 || millis() - latestValidAt > TLE_INVALID_HOLD_MS) {
    __atomic_store_n(&latestAngle, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&latestAngleValid, false, __ATOMIC_RELEASE);
  }
}

bool triggerSensorGetAngle(int16_t *angleTenthsOfDegree) {
  if (!angleTenthsOfDegree || !__atomic_load_n(&latestAngleValid, __ATOMIC_ACQUIRE)) {
    return false;
  }
  *angleTenthsOfDegree = __atomic_load_n(&latestAngle, __ATOMIC_RELAXED);
  return true;
}