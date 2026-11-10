#include "trigger_sensor.h"

#include <Arduino.h>
#include <driver/i2c_master.h>
#include <math.h>

namespace {
constexpr gpio_num_t TLE_SDA = GPIO_NUM_8;
constexpr gpio_num_t TLE_SCL = GPIO_NUM_15;
constexpr uint32_t TLE_I2C_FREQUENCY = 1000000;
constexpr uint8_t TLE_ADDRESS_A0 = 0x35;
constexpr uint8_t TLE_ADDRESS_P3 = 0x5D;
constexpr int16_t TLE_FIELD_MIN = 32;
constexpr uint32_t TLE_INVALID_HOLD_MS = 40;
constexpr int I2C_SETUP_TIMEOUT_MS = 10;
// A 6-byte read takes about 80 us at 1 MHz.
constexpr int I2C_READ_TIMEOUT_MS = 2;
constexpr uint32_t SAMPLE_PERIOD_MS = 1;
constexpr uint32_t RETRY_PERIOD_MS = 1000;
// Core 0 next to the display task (priority 1), so the I2C transfer never delays the control loop.
constexpr BaseType_t TASK_CORE = 0;
constexpr UBaseType_t TASK_PRIORITY = 3;
constexpr float HALF_TURN_TENTHS_DEGREES = 1800.0f;
constexpr float RADIANS_PER_HALF_TURN = 3.14159265f;

enum class SensorVariant : uint8_t {
  Unknown,
  A0,
  P3,
};

SensorVariant sensorVariant = SensorVariant::Unknown;
uint8_t sensorAddress = 0;
i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t device = nullptr;
bool sensorReady = false;
int16_t latestAngle = -1;
bool latestAngleValid = false;
uint32_t latestValidAt = 0;
uint32_t sampleCount = 0;

bool writeBytes(const uint8_t *bytes, uint8_t length) {
  return i2c_master_transmit(device, bytes, length, I2C_SETUP_TIMEOUT_MS) == ESP_OK;
}

bool probeAddress(uint8_t address) {
  return i2c_master_probe(bus, address, I2C_SETUP_TIMEOUT_MS) == ESP_OK;
}

void releaseBus() {
  if (device) {
    i2c_master_bus_rm_device(device);
    device = nullptr;
  }
  if (bus) {
    i2c_del_master_bus(bus);
    bus = nullptr;
  }
}

bool createBus() {
  i2c_master_bus_config_t busConfig = {};
  busConfig.i2c_port = I2C_NUM_1;
  busConfig.sda_io_num = TLE_SDA;
  busConfig.scl_io_num = TLE_SCL;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  // Same as Arduino Wire, which this replaces.
  busConfig.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&busConfig, &bus) != ESP_OK) {
    bus = nullptr;
    return false;
  }
  return true;
}

bool addDevice() {
  i2c_device_config_t deviceConfig = {};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = sensorAddress;
  deviceConfig.scl_speed_hz = TLE_I2C_FREQUENCY;
  if (i2c_master_bus_add_device(bus, &deviceConfig, &device) != ESP_OK) {
    device = nullptr;
    return false;
  }
  return true;
}

bool configureSensor() {
  if (sensorVariant == SensorVariant::P3) {
    const uint8_t configuration[] = {0x0A, 0xC6, 0x02};
    return writeBytes(configuration, sizeof(configuration));
  }

  const uint8_t resetSequence[] = {0xFF, 0xFF, 0x00, 0x00};
  for (uint8_t command : resetSequence) {
    if (!writeBytes(&command, 1)) {
      return false;
    }
  }
  delayMicroseconds(30);

  const uint8_t configuration1[] = {0x11, 0b00011001};
  const uint8_t configuration2[] = {0x10, 0b11111000};
  return writeBytes(configuration1, sizeof(configuration1)) &&
         writeBytes(configuration2, sizeof(configuration2));
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

uint8_t readCount() {
  return sensorVariant == SensorVariant::P3 ? 4 : 6;
}

bool parseAngle(const uint8_t *data, int16_t *angle) {
  const bool p3 = sensorVariant == SensorVariant::P3;
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

bool connectSensor() {
  releaseBus();
  if (!createBus()) {
    return false;
  }

  const bool hasA0 = probeAddress(TLE_ADDRESS_A0);
  const bool hasP3 = probeAddress(TLE_ADDRESS_P3);
  if (hasA0 == hasP3) {
    releaseBus();
    return false;
  }

  sensorVariant = hasA0 ? SensorVariant::A0 : SensorVariant::P3;
  sensorAddress = hasA0 ? TLE_ADDRESS_A0 : TLE_ADDRESS_P3;
  if (!addDevice() || !configureSensor()) {
    releaseBus();
    return false;
  }
  return true;
}

void readSensor() {
  uint8_t data[6];
  int16_t angle = -1;
  const bool valid = i2c_master_receive(device, data, readCount(), I2C_READ_TIMEOUT_MS) == ESP_OK &&
                     parseAngle(data, &angle);
  if (valid) {
    __atomic_store_n(&latestAngle, angle, __ATOMIC_RELAXED);
    latestValidAt = millis();
    __atomic_store_n(&latestAngleValid, true, __ATOMIC_RELEASE);
  } else if (latestValidAt == 0 || millis() - latestValidAt > TLE_INVALID_HOLD_MS) {
    __atomic_store_n(&latestAngle, -1, __ATOMIC_RELAXED);
    __atomic_store_n(&latestAngleValid, false, __ATOMIC_RELEASE);
  }
  __atomic_add_fetch(&sampleCount, 1, __ATOMIC_RELEASE);
}

void sensorTask(void *) {
  TickType_t lastWake = xTaskGetTickCount();
  for (;;) {
    if (!sensorReady) {
      __atomic_store_n(&sensorReady, connectSensor(), __ATOMIC_RELEASE);
      if (!sensorReady) {
        vTaskDelay(pdMS_TO_TICKS(RETRY_PERIOD_MS));
        lastWake = xTaskGetTickCount();
        continue;
      }
    }
    readSensor();
    xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
  }
}
}

bool triggerSensorStart() {
  static TaskHandle_t task = nullptr;
  return task || xTaskCreatePinnedToCore(sensorTask, "Trigger", 3072, nullptr, TASK_PRIORITY, &task,
                                         TASK_CORE) == pdPASS;
}

bool triggerSensorReady() {
  return __atomic_load_n(&sensorReady, __ATOMIC_ACQUIRE);
}

uint32_t triggerSensorSampleCount() {
  return __atomic_load_n(&sampleCount, __ATOMIC_ACQUIRE);
}

bool triggerSensorGetAngle(int16_t *angleTenthsOfDegree) {
  if (!angleTenthsOfDegree || !__atomic_load_n(&latestAngleValid, __ATOMIC_ACQUIRE)) {
    return false;
  }
  *angleTenthsOfDegree = __atomic_load_n(&latestAngle, __ATOMIC_RELAXED);
  return true;
}