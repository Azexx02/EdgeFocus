#include "board_config.h"
#include "dac.h"
#include "gimbal_config.h"
#include "main.h"

static bool boost_enabled;

void board_config_init(void)
{
  /* 所有驱动和 Boost 上电默认关闭。 */
  HAL_GPIO_WritePin(GIMBAL_X_EN_GPIO_Port, GIMBAL_X_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GIMBAL_Y_EN_GPIO_Port, GIMBAL_Y_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GIMBAL_X_STBY_GPIO_Port, GIMBAL_X_STBY_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GIMBAL_Y_STBY_GPIO_Port, GIMBAL_Y_STBY_Pin, GPIO_PIN_RESET);
  board_set_boost(false);
  board_set_fault(false);
  board_set_dac_mv(GIMBAL_IDLE_DAC_MV, GIMBAL_IDLE_DAC_MV);
}

void board_set_boost(bool enabled)
{
  boost_enabled = enabled;
  HAL_GPIO_WritePin(BOOST_EN_GPIO_Port, BOOST_EN_Pin,
                    enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool board_get_boost(void) { return boost_enabled; }

bool board_get_enable_input(void)
{
  return HAL_GPIO_ReadPin(G431_ENABLE_IN_GPIO_Port, G431_ENABLE_IN_Pin) == GPIO_PIN_SET;
}

void board_set_fault(bool active)
{
  HAL_GPIO_WritePin(G431_FAULT_OUT_GPIO_Port, G431_FAULT_OUT_Pin,
                    active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void board_set_dac_mv(uint16_t x_mv, uint16_t y_mv)
{
  /* DAC 采用 3.3 V 参考和 12 位右对齐数据。 */
  if (x_mv > 3300U) x_mv = 3300U;
  if (y_mv > 3300U) y_mv = 3300U;
  /* X 轴参考输出为 PA5/DAC1_OUT2，Y 轴参考输出为 PA4/DAC1_OUT1。 */
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, ((uint32_t)y_mv * 4095U) / 3300U);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_2, DAC_ALIGN_12B_R, ((uint32_t)x_mv * 4095U) / 3300U);
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_2);
}
