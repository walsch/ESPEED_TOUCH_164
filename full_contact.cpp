#include "full_contact.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include "back_emf.h"
#include "current_sensor.h"

namespace {
// Constants as in the reference (HAL.h, 4.2.0 Pro); one sample = one 1 ms control cycle.
constexpr gpio_num_t FULL_PIN = GPIO_NUM_43;
constexpr uint32_t ENGAGE_MS = 15;
// Safety margin for the contact to open before the lowside may conduct (brake, drag).
constexpr uint32_t RELEASE_MS = 15;
constexpr int32_t OC_LIMIT_MA = 50000;
constexpr uint8_t OC_SAMPLES = 2;
constexpr int32_t OC_FAST_MA = 70000;
constexpr int32_t ENGAGE_MAX_MA = 40000;
constexpr uint32_t VBUS_REL_PCT = 75;
constexpr uint32_t VBUS_STABLE_MS = 5;
constexpr uint8_t VBUS_LOW_SAMPLES = 2;
constexpr uint8_t VBUS_LOW_SAMPLES_HANDOVER = 4;
constexpr uint8_t ENGAGE_SAMPLES = 4;
constexpr uint32_t ARM_US = 7000;
constexpr uint8_t INVALID_SAMPLES = 20;
constexpr uint32_t WATCHDOG_MS = 5;
constexpr uint32_t VIN_PRESENT_MV = 5500;

volatile bool requested = false;
volatile uint32_t requestedSinceMs = 0;
volatile uint32_t releasedAtMs = 0;
volatile bool inhibited = false;
volatile bool overcurrent = false;
volatile uint32_t heartbeatMs = 0;
esp_timer_handle_t watchdogTimer = nullptr;

bool lockout = false;
uint8_t overcurrentCount = 0;
bool vbusGood = false;
uint32_t vbusGoodSinceMs = 0;
uint8_t vbusLowCount = 0;
uint8_t engageCount = 0;
uint8_t invalidCount = 0;
bool wasRequested = false;
uint32_t requestSinceUs = 0;

void setContact(bool on) {
  if (on && !inhibited) {
    if (!requested) {
      requestedSinceMs = millis();
      requested = true;
      gpio_set_level(FULL_PIN, 0);
    }
  } else {
    gpio_set_level(FULL_PIN, 1);
    if (requested) {
      releasedAtMs = millis();
    }
    requested = false;
  }
}

// Opens the contact if the control task stops updating it.
void watchdog(void *) {
  if (requested && millis() - heartbeatMs > WATCHDOG_MS) {
    gpio_set_level(FULL_PIN, 1);
    releasedAtMs = millis();
    requested = false;
  }
}
}

void fullContactBegin() {
  gpio_set_level(FULL_PIN, 1);
  gpio_set_direction(FULL_PIN, GPIO_MODE_OUTPUT);
  const esp_timer_create_args_t args = {.callback = watchdog, .name = "fullWdg"};
  if (esp_timer_create(&args, &watchdogTimer) == ESP_OK) {
    esp_timer_start_periodic(watchdogTimer, 1000);
  }
}

void fullContactUpdate(bool enabled, bool triggerFull, bool outputFull, uint32_t vinMilliVolts) {
  heartbeatMs = millis();
  const bool isRequested = requested;
  if (isRequested && !wasRequested) {
    requestSinceUs = micros();
  }
  wasRequested = isRequested;
  const uint32_t requestAgeUs = isRequested ? micros() - requestSinceUs : 0;
  const bool armed = isRequested && requestAgeUs >= ARM_US;
  const bool handover = isRequested && requestAgeUs < ENGAGE_MS * 1000UL + 1000UL;

  if (!triggerFull) {
    // Overcurrent lockout ends when the trigger leaves 100 %.
    lockout = false;
    overcurrentCount = 0;
  }

  const bool atFull = triggerFull && outputFull;
  if (atFull) {
    uint32_t motorMilliVolts = 0;
    const bool valid = backEmfReadMotorMilliVolts(&motorMilliVolts);
    const bool good = valid && motorMilliVolts >= VIN_PRESENT_MV &&
                      (vinMilliVolts < VIN_PRESENT_MV || motorMilliVolts * 100UL >= vinMilliVolts * VBUS_REL_PCT);
    const uint8_t lowLimit = handover ? VBUS_LOW_SAMPLES_HANDOVER : VBUS_LOW_SAMPLES;
    if (good) {
      vbusLowCount = 0;
      if (!vbusGood) {
        vbusGood = true;
        vbusGoodSinceMs = millis();
      }
    } else {
      if (vbusLowCount < 255) {
        vbusLowCount++;
      }
      if (vbusLowCount >= lowLimit) {
        vbusGood = false;
      }
    }
  } else {
    vbusGood = false;
    vbusLowCount = 0;
  }

  int32_t milliAmps = 0;
  const bool currentValid = currentSensorGetLatestMilliAmps(&milliAmps);
  if (atFull && !isRequested && currentValid) {
    if (labs(milliAmps) < ENGAGE_MAX_MA) {
      if (engageCount < ENGAGE_SAMPLES) {
        engageCount++;
      }
    } else {
      engageCount = 0;
    }
  } else if (!(atFull && !isRequested)) {
    engageCount = 0;
  }

  const bool supplyOk = vbusGood && millis() - vbusGoodSinceMs >= VBUS_STABLE_MS;
  const bool candidate = atFull && enabled && !lockout && supplyOk;
  if (currentValid) {
    invalidCount = 0;
  } else if (invalidCount < 255) {
    invalidCount++;
  }
  const bool sensorLost = invalidCount >= INVALID_SAMPLES;

  bool want = false;
  if (candidate && isRequested) {
    want = !sensorLost;
    if (want && currentValid) {
      if (milliAmps > OC_FAST_MA) {
        overcurrentCount = OC_SAMPLES;
      } else if (milliAmps > OC_LIMIT_MA) {
        overcurrentCount++;
      } else {
        overcurrentCount = 0;
      }
      if (overcurrentCount >= OC_SAMPLES) {
        want = false;
        overcurrentCount = 0;
        if (armed) {
          lockout = true;
          overcurrent = true;
        }
      }
    }
  } else if (candidate) {
    want = engageCount >= ENGAGE_SAMPLES && !sensorLost;
    overcurrentCount = 0;
  }
  if (want && !isRequested) {
    overcurrent = false;
  }
  setContact(want);
}

bool fullContactRequested() {
  return requested;
}

bool fullContactEngaged() {
  return requested && millis() - requestedSinceMs >= ENGAGE_MS;
}

bool fullContactOvercurrent() {
  return overcurrent;
}

bool fullContactLowSideAllowed() {
  return !requested && millis() - releasedAtMs >= RELEASE_MS;
}

void fullContactInhibit(bool inhibit) {
  inhibited = inhibit;
  if (inhibit) {
    setContact(false);
  }
}
