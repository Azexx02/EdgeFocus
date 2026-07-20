#include "current_sample.h"

#include "adc.h"
#include "main.h"

#include <string.h>

#define CURRENT_WINDOW_SAMPLES 100U
#define CURRENT_SAMPLE_PERIOD_MS 10U
#define CURRENT_MAX_AGE_MS 30U
#define CURRENT_SATURATION_RAW 4090U

static uint16_t samples[2][CURRENT_WINDOW_SAMPLES];
static uint32_t sums[2];
static uint16_t raw_value[2];
static uint16_t peak_value[2];
static uint16_t write_index;
static uint16_t window_count;
static uint32_t total_since_reset;
static uint32_t last_sample_tick;
static bool saturated_since_reset;

bool current_sample_init(void)
{
  memset(samples, 0, sizeof(samples));
  memset(sums, 0, sizeof(sums));
  memset(raw_value, 0, sizeof(raw_value));
  memset(peak_value, 0, sizeof(peak_value));
  write_index = 0U;
  window_count = 0U;
  total_since_reset = 0U;
  last_sample_tick = HAL_GetTick();
  saturated_since_reset = false;
  return HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) == HAL_OK;
}

bool current_sample_update(void)
{
  uint16_t next[2];
  unsigned axis;
  if (HAL_ADC_Start(&hadc1) != HAL_OK) return false;
  if (HAL_ADC_PollForConversion(&hadc1, 2U) != HAL_OK) goto fail;
  next[1] = (uint16_t)HAL_ADC_GetValue(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 2U) != HAL_OK) goto fail;
  next[0] = (uint16_t)HAL_ADC_GetValue(&hadc1);
  (void)HAL_ADC_Stop(&hadc1);
  for (axis = 0U; axis < 2U; ++axis) {
    if (window_count == CURRENT_WINDOW_SAMPLES) {
      sums[axis] -= samples[axis][write_index];
    }
    samples[axis][write_index] = next[axis];
    sums[axis] += next[axis];
    raw_value[axis] = next[axis];
    if (next[axis] > peak_value[axis]) peak_value[axis] = next[axis];
    if (next[axis] >= CURRENT_SATURATION_RAW) saturated_since_reset = true;
  }
  write_index = (uint16_t)((write_index + 1U) % CURRENT_WINDOW_SAMPLES);
  if (window_count < CURRENT_WINDOW_SAMPLES) ++window_count;
  if (total_since_reset < UINT32_MAX) ++total_since_reset;
  last_sample_tick = HAL_GetTick();
  return true;
fail:
  (void)HAL_ADC_Stop(&hadc1);
  return false;
}

void current_sample_reset_statistics(void)
{
  peak_value[0] = raw_value[0];
  peak_value[1] = raw_value[1];
  total_since_reset = 0U;
  saturated_since_reset = false;
}

void current_sample_snapshot(CurrentSampleSnapshot *snapshot)
{
  uint32_t primask;
  unsigned axis;
  if (snapshot == NULL) return;
  primask = __get_PRIMASK();
  __disable_irq();
  memset(snapshot, 0, sizeof(*snapshot));
  for (axis = 0U; axis < 2U; ++axis) {
    snapshot->raw[axis] = raw_value[axis];
    snapshot->peak[axis] = peak_value[axis];
    snapshot->average[axis] = window_count == 0U ? 0U :
        (uint16_t)(sums[axis] / window_count);
  }
  snapshot->window_ms = (uint32_t)window_count * CURRENT_SAMPLE_PERIOD_MS;
  snapshot->samples_since_reset = total_since_reset;
  snapshot->sample_age_ms = HAL_GetTick() - last_sample_tick;
  snapshot->saturated = saturated_since_reset;
  snapshot->valid = window_count == CURRENT_WINDOW_SAMPLES &&
                    snapshot->sample_age_ms <= CURRENT_MAX_AGE_MS &&
                    !snapshot->saturated;
  __set_PRIMASK(primask);
}

uint16_t current_get_x_raw(void) { return raw_value[0]; }
uint16_t current_get_y_raw(void) { return raw_value[1]; }
