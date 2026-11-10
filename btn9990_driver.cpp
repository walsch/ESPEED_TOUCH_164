#include "btn9990_driver.h"

#include <Arduino.h>
#include <driver/mcpwm_prelude.h>

namespace {
constexpr gpio_num_t BTN9990_IN_PIN = GPIO_NUM_5;
constexpr gpio_num_t BTN9990_INH_PIN = GPIO_NUM_7;
// 40 MHz gives 0.075 % steps at 30 kHz but needs >= 611 Hz (16-bit period); below that 10 MHz.
constexpr uint32_t RESOLUTION_FINE_HZ = 40000000;
constexpr uint32_t RESOLUTION_COARSE_HZ = 10000000;
constexpr uint32_t MAX_PERIOD_TICKS = 65535;
constexpr uint32_t DEFAULT_FREQUENCY_HZ = 30000;
constexpr uint32_t PWM_FREQUENCY_MIN_HZ = 200;
constexpr uint32_t PWM_FREQUENCY_MAX_HZ = 50000;

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
// Applied duties in 0.1 % (0 ... 1000).
uint16_t appliedInputDuty = 0;
uint16_t appliedInhibitDuty = 0;
uint32_t timerResolutionHz = RESOLUTION_FINE_HZ;
bool fineResolutionAvailable = true;
uint32_t periodTicks = RESOLUTION_FINE_HZ / DEFAULT_FREQUENCY_HZ;
uint32_t driveFrequencyHz = DEFAULT_FREQUENCY_HZ;
uint32_t brakeFrequencyHz = DEFAULT_FREQUENCY_HZ;

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
                      uint16_t dutyPermille) {
  if (generator == inputGenerator) {
    appliedInputDuty = dutyPermille;
  } else {
    appliedInhibitDuty = dutyPermille;
  }
  if (dutyPermille == 0) {
    return mcpwm_generator_set_force_level(generator, 0, true) == ESP_OK;
  }
  if (dutyPermille >= 1000) {
    return mcpwm_generator_set_force_level(generator, 1, true) == ESP_OK;
  }

  uint32_t compareTicks = (periodTicks * dutyPermille + 500U) / 1000U;
  compareTicks = constrain(compareTicks, 1U, periodTicks - 1U);
  // A held force level is only released with hold_on=true.
  return mcpwm_comparator_set_compare_value(comparator, compareTicks) == ESP_OK &&
         mcpwm_generator_set_force_level(generator, -1, true) == ESP_OK;
}

bool applyFrequency(uint32_t frequencyHz) {
  const uint32_t hz = constrain(frequencyHz, PWM_FREQUENCY_MIN_HZ, PWM_FREQUENCY_MAX_HZ);
  const uint32_t resolution = fineResolutionAvailable && RESOLUTION_FINE_HZ / hz <= MAX_PERIOD_TICKS
                                  ? RESOLUTION_FINE_HZ
                                  : RESOLUTION_COARSE_HZ;
  const uint32_t ticks = resolution / hz;
  if (resolution == timerResolutionHz && ticks == periodTicks) {
    return true;
  }
  if (resolution == timerResolutionHz) {
    if (mcpwm_timer_set_period(pwmTimer, ticks) != ESP_OK) {
      return false;
    }
    periodTicks = ticks;
  } else {
    // Resolution is fixed per timer: rebuild (~0.4 ms with both switches off).
    const uint16_t inputDuty = appliedInputDuty;
    const uint16_t inhibitDuty = appliedInhibitDuty;
    const bool enabled = outputEnabled;
    cleanupPwm();
    timerResolutionHz = resolution;
    periodTicks = ticks;
    if (!btn9990Begin()) {
      return false;
    }
    outputEnabled = enabled;
    appliedInputDuty = inputDuty;
    appliedInhibitDuty = inhibitDuty;
  }
  // Compare values scale with the period.
  return setGeneratorDuty(inputGenerator, inputComparator, appliedInputDuty) &&
         setGeneratorDuty(inhibitGenerator, inhibitComparator, appliedInhibitDuty);
}
}

bool btn9990Begin() {
  if (driverReady) {
    return true;
  }

  forcePinsLow();


  mcpwm_timer_config_t timerConfig = {
      .group_id = 0,
      .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
      .resolution_hz = timerResolutionHz,
      .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
      .period_ticks = periodTicks,
      .flags = {.update_period_on_empty = true},
  };
  if (mcpwm_new_timer(&timerConfig, &pwmTimer) != ESP_OK) {
    if (timerResolutionHz == RESOLUTION_COARSE_HZ) {
      cleanupPwm();
      return false;
    }
    const uint32_t hz = timerResolutionHz / periodTicks;
    fineResolutionAvailable = false;
    timerResolutionHz = RESOLUTION_COARSE_HZ;
    periodTicks = RESOLUTION_COARSE_HZ / hz;
    timerConfig.resolution_hz = timerResolutionHz;
    timerConfig.period_ticks = periodTicks;
    if (mcpwm_new_timer(&timerConfig, &pwmTimer) != ESP_OK) {
      cleanupPwm();
      return false;
    }
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

  if (mcpwm_comparator_set_compare_value(inputComparator, periodTicks / 2) != ESP_OK ||
      mcpwm_comparator_set_compare_value(inhibitComparator, periodTicks / 2) != ESP_OK ||
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
  }
  return true;
}

void btn9990Disable() {
  outputEnabled = false;
  brakeActive = false;
  if (driverReady) {
    appliedInputDuty = 0;
    appliedInhibitDuty = 0;
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
  applyFrequency(brakeFrequencyHz);
  const uint16_t inhibitDuty = static_cast<uint16_t>(brakePercent) * 10U;
  if (!setGeneratorDuty(inputGenerator, inputComparator, 0) ||
      !setGeneratorDuty(inhibitGenerator, inhibitComparator, inhibitDuty)) {
    btn9990Disable();
    return false;
  }
  brakeActive = brakePercent > 0;
  return true;
}

void btn9990SetSupplyAvailable(bool available, uint8_t brakePercent) {
  if (!available) {
    btn9990SetBrakeDuty(brakePercent);
  }
}

bool btn9990SetOutput(uint16_t throttlePermille, uint16_t dragPermille) {
  if (!driverReady) {
    return false;
  }
  if (!outputEnabled) {
    return false;
  }
  if (throttlePermille > 1000 || dragPermille > 1000) {
    btn9990SetBrakeDuty(100);
    return false;
  }

  const uint16_t inhibitDuty = min<uint16_t>(1000, throttlePermille + dragPermille);
  applyFrequency(driveFrequencyHz);
  if (!setGeneratorDuty(inputGenerator, inputComparator, throttlePermille) ||
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

bool btn9990IsOutputEnabled() {
  return outputEnabled;
}

void btn9990SetFrequencies(uint32_t driveHz, uint32_t brakeHz) {
  driveFrequencyHz = driveHz;
  brakeFrequencyHz = brakeHz;
}

void btn9990PauseOutput() {
  if (driverReady) {
    mcpwm_generator_set_force_level(inputGenerator, 0, true);
    mcpwm_generator_set_force_level(inhibitGenerator, 0, true);
  }
}

void btn9990ResumeOutput() {
  if (driverReady) {
    setGeneratorDuty(inputGenerator, inputComparator, appliedInputDuty);
    setGeneratorDuty(inhibitGenerator, inhibitComparator, appliedInhibitDuty);
  }
}
