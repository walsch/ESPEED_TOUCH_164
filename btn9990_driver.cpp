#include "btn9990_driver.h"

#include <Arduino.h>
#include <driver/mcpwm_prelude.h>

namespace {
constexpr gpio_num_t BTN9990_IS_PIN = GPIO_NUM_1;
constexpr gpio_num_t BTN9990_IN_PIN = GPIO_NUM_5;
constexpr gpio_num_t BTN9990_INH_PIN = GPIO_NUM_7;
constexpr uint32_t MCPWM_RESOLUTION_HZ = 10000000;
constexpr uint32_t MCPWM_PERIOD_TICKS = 333;
constexpr uint16_t BTN9990_SENSE_RESISTOR_OHMS = 2000;
constexpr float BTN9990_KTIS_AMPS_PER_KELVIN = 3.72e-6f;

mcpwm_timer_handle_t pwmTimer = nullptr;
mcpwm_oper_handle_t pwmOperator = nullptr;
mcpwm_cmpr_handle_t inputComparator = nullptr;
mcpwm_cmpr_handle_t inhibitComparator = nullptr;
mcpwm_gen_handle_t inputGenerator = nullptr;
mcpwm_gen_handle_t inhibitGenerator = nullptr;
bool timerEnabled = false;
bool timerRunning = false;
bool driverReady = false;
bool outputEnabled = false;
bool brakeActive = false;
uint8_t activeBrakeDuty = 100;
bool supplyAvailable = false;
int16_t latestTemperatureDeciC = 0;
bool latestTemperatureValid = false;
uint16_t latestSenseMilliVolts = 0;
bool latestSenseValid = false;

void forcePinsLow() {
  digitalWrite(static_cast<uint8_t>(BTN9990_IN_PIN), LOW);
  digitalWrite(static_cast<uint8_t>(BTN9990_INH_PIN), LOW);
  pinMode(static_cast<uint8_t>(BTN9990_IN_PIN), OUTPUT);
  pinMode(static_cast<uint8_t>(BTN9990_INH_PIN), OUTPUT);
}

void cleanupPwm() {
  if (inputGenerator) {
    mcpwm_generator_set_force_level(inputGenerator, 0, true);
  }
  if (inhibitGenerator) {
    mcpwm_generator_set_force_level(inhibitGenerator, 0, true);
  }
  if (timerRunning) {
    mcpwm_timer_start_stop(pwmTimer, MCPWM_TIMER_STOP_EMPTY);
    timerRunning = false;
  }
  if (timerEnabled) {
    mcpwm_timer_disable(pwmTimer);
    timerEnabled = false;
  }
  if (inputGenerator) {
    mcpwm_del_generator(inputGenerator);
    inputGenerator = nullptr;
  }
  if (inhibitGenerator) {
    mcpwm_del_generator(inhibitGenerator);
    inhibitGenerator = nullptr;
  }
  if (inputComparator) {
    mcpwm_del_comparator(inputComparator);
    inputComparator = nullptr;
  }
  if (inhibitComparator) {
    mcpwm_del_comparator(inhibitComparator);
    inhibitComparator = nullptr;
  }
  if (pwmOperator) {
    mcpwm_del_operator(pwmOperator);
    pwmOperator = nullptr;
  }
  if (pwmTimer) {
    mcpwm_del_timer(pwmTimer);
    pwmTimer = nullptr;
  }
  forcePinsLow();
  driverReady = false;
  outputEnabled = false;
}

bool configureGenerator(mcpwm_gen_handle_t generator, mcpwm_cmpr_handle_t comparator) {
  return mcpwm_generator_set_action_on_timer_event(
             generator,
             MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                          MCPWM_TIMER_EVENT_EMPTY,
                                          MCPWM_GEN_ACTION_HIGH)) == ESP_OK &&
         mcpwm_generator_set_action_on_compare_event(
             generator,
             MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP,
                                            comparator,
                                            MCPWM_GEN_ACTION_LOW)) == ESP_OK &&
         mcpwm_generator_set_force_level(generator, 0, true) == ESP_OK;
}

bool setGeneratorDuty(mcpwm_gen_handle_t generator, mcpwm_cmpr_handle_t comparator,
                      uint8_t duty) {
  if (duty == 0) {
    return mcpwm_generator_set_force_level(generator, 0, true) == ESP_OK;
  }
  if (duty == 255) {
    return mcpwm_generator_set_force_level(generator, 1, true) == ESP_OK;
  }

  uint32_t compareTicks = (MCPWM_PERIOD_TICKS * duty + 127U) / 255U;
  compareTicks = constrain(compareTicks, 1U, MCPWM_PERIOD_TICKS - 1U);
  // A held force level is only released with hold_on=true.
  return mcpwm_comparator_set_compare_value(comparator, compareTicks) == ESP_OK &&
         mcpwm_generator_set_force_level(generator, -1, true) == ESP_OK;
}
}

bool btn9990Begin() {
  if (driverReady) {
    return true;
  }

  forcePinsLow();
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(static_cast<uint8_t>(BTN9990_IS_PIN), INPUT);
  analogSetPinAttenuation(static_cast<uint8_t>(BTN9990_IS_PIN), ADC_11db);
  (void)analogReadMilliVolts(static_cast<uint8_t>(BTN9990_IS_PIN));

  const mcpwm_timer_config_t timerConfig = {
      .group_id = 0,
      .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
      .resolution_hz = MCPWM_RESOLUTION_HZ,
      .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
      .period_ticks = MCPWM_PERIOD_TICKS,
  };
  if (mcpwm_new_timer(&timerConfig, &pwmTimer) != ESP_OK) {
    cleanupPwm();
    return false;
  }

  const mcpwm_operator_config_t operatorConfig = {.group_id = 0};
  if (mcpwm_new_operator(&operatorConfig, &pwmOperator) != ESP_OK ||
      mcpwm_operator_connect_timer(pwmOperator, pwmTimer) != ESP_OK) {
    cleanupPwm();
    return false;
  }

  const mcpwm_comparator_config_t comparatorConfig = {
      .flags = {.update_cmp_on_tez = true},
  };
  if (mcpwm_new_comparator(pwmOperator, &comparatorConfig, &inputComparator) != ESP_OK ||
      mcpwm_new_comparator(pwmOperator, &comparatorConfig, &inhibitComparator) != ESP_OK) {
    cleanupPwm();
    return false;
  }

  const mcpwm_generator_config_t inputGeneratorConfig = {
      .gen_gpio_num = static_cast<int>(BTN9990_IN_PIN),
  };
  const mcpwm_generator_config_t inhibitGeneratorConfig = {
      .gen_gpio_num = static_cast<int>(BTN9990_INH_PIN),
  };
  if (mcpwm_new_generator(pwmOperator, &inputGeneratorConfig, &inputGenerator) != ESP_OK ||
      mcpwm_new_generator(pwmOperator, &inhibitGeneratorConfig, &inhibitGenerator) != ESP_OK ||
      !configureGenerator(inputGenerator, inputComparator) ||
      !configureGenerator(inhibitGenerator, inhibitComparator)) {
    cleanupPwm();
    return false;
  }

  if (mcpwm_comparator_set_compare_value(inputComparator, MCPWM_PERIOD_TICKS / 2) != ESP_OK ||
      mcpwm_comparator_set_compare_value(inhibitComparator, MCPWM_PERIOD_TICKS / 2) != ESP_OK ||
      mcpwm_timer_enable(pwmTimer) != ESP_OK) {
    cleanupPwm();
    return false;
  }
  timerEnabled = true;
  if (mcpwm_timer_start_stop(pwmTimer, MCPWM_TIMER_START_NO_STOP) != ESP_OK) {
    cleanupPwm();
    return false;
  }
  timerRunning = true;
  driverReady = true;
  outputEnabled = false;
  return true;
}

bool btn9990Enable() {
  if (!driverReady) {
    return false;
  }
  if (!outputEnabled) {
    outputEnabled = true;
    brakeActive = false;
    __atomic_store_n(&latestTemperatureValid, false, __ATOMIC_RELEASE);
    __atomic_store_n(&latestSenseValid, false, __ATOMIC_RELEASE);
  }
  return true;
}

void btn9990Disable() {
  outputEnabled = false;
  brakeActive = false;
  if (driverReady) {
    mcpwm_generator_set_force_level(inputGenerator, 0, true);
    mcpwm_generator_set_force_level(inhibitGenerator, 0, true);
  } else {
    forcePinsLow();
  }
}

bool btn9990SetBrake() {
  return btn9990SetBrakeDuty(100);
}

bool btn9990SetBrakeDuty(uint8_t brakePercent) {
  const bool modeChanged = outputEnabled || !brakeActive || activeBrakeDuty != brakePercent;
  outputEnabled = false;
  if (!driverReady) {
    forcePinsLow();
    brakeActive = false;
    return false;
  }
  if (brakePercent > 100) {
    brakePercent = 100;
  }
  activeBrakeDuty = brakePercent;
  const uint8_t inhibitDuty = static_cast<uint16_t>(brakePercent) * 255U / 100U;
  if (!setGeneratorDuty(inputGenerator, inputComparator, 0) ||
      !setGeneratorDuty(inhibitGenerator, inhibitComparator, inhibitDuty)) {
    btn9990Disable();
    return false;
  }
  brakeActive = brakePercent > 0;
  if (modeChanged) {
    __atomic_store_n(&latestTemperatureValid, false, __ATOMIC_RELEASE);
    __atomic_store_n(&latestSenseValid, false, __ATOMIC_RELEASE);
  }
  return true;
}

void btn9990SetSupplyAvailable(bool available, uint8_t brakePercent) {
  supplyAvailable = available;
  if (!available) {
    btn9990SetBrakeDuty(brakePercent);
    __atomic_store_n(&latestTemperatureValid, false, __ATOMIC_RELEASE);
    __atomic_store_n(&latestSenseValid, false, __ATOMIC_RELEASE);
  }
}

bool btn9990SetOutput(uint8_t throttlePercent, uint8_t dragPercent) {
  if (!driverReady) {
    return false;
  }
  if (!outputEnabled) {
    return false;
  }
  if (throttlePercent > 100 || dragPercent > 100) {
    btn9990SetBrakeDuty(100);
    return false;
  }

  const uint8_t inputDuty = static_cast<uint16_t>(throttlePercent) * 255U / 100U;
  const uint16_t dragDuty = static_cast<uint16_t>(dragPercent) * 255U / 100U;
  const uint8_t inhibitDuty = (inputDuty + dragDuty > 255U) ? 255U : inputDuty + dragDuty;
  if (!setGeneratorDuty(inputGenerator, inputComparator, inputDuty) ||
      !setGeneratorDuty(inhibitGenerator, inhibitComparator, inhibitDuty)) {
    btn9990SetBrakeDuty(100);
    return false;
  }
  brakeActive = false;
  return true;
}

bool btn9990IsReady() {
  return driverReady;
}

bool btn9990SampleTemperature() {
  if (!driverReady || !supplyAvailable || outputEnabled) {
    __atomic_store_n(&latestTemperatureValid, false, __ATOMIC_RELEASE);
    __atomic_store_n(&latestSenseValid, false, __ATOMIC_RELEASE);
    return false;
  }

  const bool restoreBrake = brakeActive;
  if (mcpwm_generator_set_force_level(inhibitGenerator, 0, true) != ESP_OK ||
      mcpwm_generator_set_force_level(inputGenerator, 1, true) != ESP_OK) {
    if (restoreBrake) {
      btn9990SetBrake();
    } else {
      btn9990Disable();
    }
    __atomic_store_n(&latestTemperatureValid, false, __ATOMIC_RELEASE);
    __atomic_store_n(&latestSenseValid, false, __ATOMIC_RELEASE);
    return false;
  }

  delayMicroseconds(7);
  const uint32_t senseMilliVolts = analogReadMilliVolts(static_cast<uint8_t>(BTN9990_IS_PIN));
  __atomic_store_n(&latestSenseMilliVolts, static_cast<uint16_t>(senseMilliVolts), __ATOMIC_RELAXED);
  __atomic_store_n(&latestSenseValid, true, __ATOMIC_RELEASE);
  mcpwm_generator_set_force_level(inputGenerator, 0, true);
  if (restoreBrake) {
    const uint8_t inhibitDuty = static_cast<uint16_t>(activeBrakeDuty) * 255U / 100U;
    setGeneratorDuty(inhibitGenerator, inhibitComparator, inhibitDuty);
  } else {
    mcpwm_generator_set_force_level(inhibitGenerator, 0, true);
  }

  const float senseCurrentAmps = (senseMilliVolts / 1000.0f) / BTN9990_SENSE_RESISTOR_OHMS;
  const float temperatureKelvin = senseCurrentAmps / BTN9990_KTIS_AMPS_PER_KELVIN;
  if (senseMilliVolts == 0 || temperatureKelvin < 150.0f || temperatureKelvin > 500.0f) {
    __atomic_store_n(&latestTemperatureValid, false, __ATOMIC_RELEASE);
    return false;
  }

  const int16_t temperatureDeciC = lroundf((temperatureKelvin - 273.15f) * 10.0f);
  __atomic_store_n(&latestTemperatureDeciC, temperatureDeciC, __ATOMIC_RELAXED);
  __atomic_store_n(&latestTemperatureValid, true, __ATOMIC_RELEASE);
  return true;
}

bool btn9990GetTemperatureDeciC(int16_t *temperatureDeciC) {
  if (!temperatureDeciC || !__atomic_load_n(&latestTemperatureValid, __ATOMIC_ACQUIRE)) {
    return false;
  }
  *temperatureDeciC = __atomic_load_n(&latestTemperatureDeciC, __ATOMIC_RELAXED);
  return true;
}

bool btn9990GetSenseMilliVolts(uint16_t *senseMilliVolts) {
  if (!senseMilliVolts || !__atomic_load_n(&latestSenseValid, __ATOMIC_ACQUIRE)) {
    return false;
  }
  *senseMilliVolts = __atomic_load_n(&latestSenseMilliVolts, __ATOMIC_RELAXED);
  return true;
}