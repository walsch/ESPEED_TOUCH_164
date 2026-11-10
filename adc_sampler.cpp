#include "adc_sampler.h"

#include <Arduino.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_continuous.h>
#include <esp_ipc.h>

namespace {
// ADC1 channel n is GPIO n+1 on the ESP32-S3.
constexpr adc_channel_t CHANNELS[] = {ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3};
// Motor voltage every 2nd slot (25 us) for the BEMF settle check, current every 50 us,
// VIN and VCC every 100 us at 80 kS/s.
constexpr AdcInput PATTERN[] = {
    AdcInput::Current, AdcInput::MotorVoltage, AdcInput::TrackVoltage, AdcInput::MotorVoltage,
    AdcInput::Current, AdcInput::MotorVoltage, AdcInput::Supply, AdcInput::MotorVoltage,
};
constexpr uint32_t PATTERN_LENGTH = sizeof(PATTERN) / sizeof(PATTERN[0]);
constexpr uint32_t SAMPLE_FREQUENCY_HZ = 80000;
// One frame per pattern: one DMA interrupt every 100 us.
constexpr uint32_t FRAME_BYTES = PATTERN_LENGTH * SOC_ADC_DIGI_RESULT_BYTES;
constexpr uint8_t INPUT_COUNT = static_cast<uint8_t>(AdcInput::Count);

adc_continuous_handle_t adcHandle = nullptr;
adc_cali_handle_t caliHandle = nullptr;
volatile uint16_t latestRaw[INPUT_COUNT] = {};
volatile bool latestValid[INPUT_COUNT] = {};
volatile uint32_t motorSequence = 0;

bool IRAM_ATTR onFrame(adc_continuous_handle_t, const adc_continuous_evt_data_t *data, void *) {
  const uint32_t count = data->size / SOC_ADC_DIGI_RESULT_BYTES;
  for (uint32_t index = 0; index < count; index++) {
    const adc_digi_output_data_t *result =
        reinterpret_cast<const adc_digi_output_data_t *>(&data->conv_frame_buffer[index * SOC_ADC_DIGI_RESULT_BYTES]);
    // CHANNELS[] is in AdcInput order with channel n at index n.
    const uint32_t input = result->type2.channel;
    if (input >= INPUT_COUNT) {
      continue;
    }
    latestRaw[input] = result->type2.data;
    latestValid[input] = true;
    if (input == static_cast<uint32_t>(AdcInput::MotorVoltage)) {
      motorSequence = motorSequence + 1;
    }
  }
  return false;
}

void startOnCurrentCore(void *result) {
  const adc_continuous_handle_cfg_t handleConfig = {
      .max_store_buf_size = FRAME_BYTES * 4,
      .conv_frame_size = FRAME_BYTES,
  };
  if (adc_continuous_new_handle(&handleConfig, &adcHandle) != ESP_OK) {
    adcHandle = nullptr;
    return;
  }

  adc_digi_pattern_config_t pattern[PATTERN_LENGTH] = {};
  for (uint32_t index = 0; index < PATTERN_LENGTH; index++) {
    pattern[index].atten = ADC_ATTEN_DB_12;
    pattern[index].channel = CHANNELS[static_cast<uint8_t>(PATTERN[index])];
    pattern[index].unit = ADC_UNIT_1;
    pattern[index].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  }
  const adc_continuous_config_t config = {
      .pattern_num = PATTERN_LENGTH,
      .adc_pattern = pattern,
      .sample_freq_hz = SAMPLE_FREQUENCY_HZ,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
      .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
  };
  const adc_continuous_evt_cbs_t callbacks = {.on_conv_done = onFrame};
  const adc_cali_curve_fitting_config_t caliConfig = {
      .unit_id = ADC_UNIT_1,
      .chan = ADC_CHANNEL_0,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  *static_cast<bool *>(result) =
      adc_continuous_config(adcHandle, &config) == ESP_OK &&
      adc_continuous_register_event_callbacks(adcHandle, &callbacks, nullptr) == ESP_OK &&
      adc_cali_create_scheme_curve_fitting(&caliConfig, &caliHandle) == ESP_OK &&
      adc_continuous_start(adcHandle) == ESP_OK;
}
}

bool adcSamplerBegin() {
  static bool started = false;
  if (!adcHandle) {
    // The interrupt fires every 100 us; it is allocated on the calling core, so start it on core 0
    // to keep that load away from the control loop on core 1.
    esp_ipc_call_blocking(0, startOnCurrentCore, &started);
  }
  return started;
}

bool adcSamplerRead(AdcInput input, uint16_t *milliVolts) {
  const uint8_t index = static_cast<uint8_t>(input);
  if (!milliVolts || index >= INPUT_COUNT || !latestValid[index] || !caliHandle) {
    return false;
  }
  int voltage = 0;
  if (adc_cali_raw_to_voltage(caliHandle, latestRaw[index], &voltage) != ESP_OK) {
    return false;
  }
  *milliVolts = static_cast<uint16_t>(max(0, voltage));
  return true;
}

uint32_t adcSamplerMotorSequence() {
  return motorSequence;
}
