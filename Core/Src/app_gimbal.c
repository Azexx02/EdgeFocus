#include "app_gimbal.h"
#include "board_config.h"
#include "current_sample.h"
#include "motor_pwm.h"
#include "soft_i2c.h"
#include "uart_protocol.h"
#include "main.h"

void app_gimbal_init(void)
{
  /* 上电顺序以功率输出保持关闭为首要原则。 */
  board_config_init();
  soft_i2c_init();
  motor_pwm_init();
  current_sample_init();
  uart_protocol_init();
}

void app_gimbal_process(void)
{
  static uint32_t next_sample;
  uart_protocol_process();
  uint32_t now = HAL_GetTick();
  /* 前期以 100 Hz 更新电流采样，不在此处运行高速电流环。 */
  if ((int32_t)(now - next_sample) >= 0) {
    current_sample_update();
    next_sample = now + 10U;
  }
}
