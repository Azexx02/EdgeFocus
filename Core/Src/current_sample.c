#include "current_sample.h"
#include "adc.h"

static uint16_t adc_in2_raw;
static uint16_t adc_in3_raw;

bool current_sample_init(void)
{
  /* 上电后先完成单端 ADC 校准。 */
  return HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) == HAL_OK;
}

bool current_sample_update(void)
{
  /* 扫描顺序与 CubeMX Rank 1/Rank 2 保持一致。 */
  if (HAL_ADC_Start(&hadc1) != HAL_OK) return false;
  if (HAL_ADC_PollForConversion(&hadc1, 2U) != HAL_OK) goto fail;
  adc_in2_raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 2U) != HAL_OK) goto fail;
  adc_in3_raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);
  return true;
fail:
  HAL_ADC_Stop(&hadc1);
  return false;
}

/* X 轴电流由 PA2/ADC1_IN3 采样，Y 轴电流由 PA1/ADC1_IN2 采样。 */
uint16_t current_get_x_raw(void) { return adc_in3_raw; }
uint16_t current_get_y_raw(void) { return adc_in2_raw; }
