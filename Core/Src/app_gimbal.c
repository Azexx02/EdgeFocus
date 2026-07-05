#include "app_gimbal.h"
#include "board_config.h"
#include "current_sample.h"
#include "gimbal_config.h"
#include "gimbal_storage.h"
#include "motor_pwm.h"
#include "soft_i2c.h"
#include "uart_protocol.h"
#include "main.h"

#if GIMBAL_CONTROL_ENABLE
#if GIMBAL_AUTO_HOME_ON_BOOT
static bool startup_homing_pending;
static uint32_t startup_homing_tick;
#endif

static void gimbal_control_process(void)
{
#if GIMBAL_REQUIRE_ENABLE_INPUT
  if (!board_get_enable_input()) {
    motor_pwm_set_enabled(false);
    board_set_boost(false);
    motor_pwm_hold_enable(false);
    return;
  }
#endif
#if GIMBAL_AUTO_HOME_ON_BOOT
  if (startup_homing_pending &&
      (int32_t)(HAL_GetTick() - startup_homing_tick) >= 0) {
    startup_homing_pending = false;
    uart_protocol_send("BOOT_HOME,X_START\r\n");
    uart_protocol_set_busy(1U);
    bool ok = motor_pwm_auto_home_axis(GIMBAL_AXIS_X, NULL);
    uart_protocol_set_busy(0U);
    uart_protocol_send(ok ? "BOOT_HOME,X_OK\r\n" :
                            "ERR,BOOT_HOME_X\r\n");
  }
#endif
  motor_pwm_hold_process();
}
#endif

void app_gimbal_init(void)
{
  /* 上电顺序以功率输出保持关闭为首要原则。 */
  board_config_init();
  soft_i2c_init();
  motor_pwm_init();
  (void)gimbal_storage_load();
  current_sample_init();
  uart_protocol_init();
#if GIMBAL_CONTROL_ENABLE && GIMBAL_AUTO_HOME_ON_BOOT
  startup_homing_pending = true;
  startup_homing_tick = HAL_GetTick() + 800U;
#endif
}

void app_gimbal_process(void)
{
  static uint32_t next_sample;
  uart_protocol_process();
#if GIMBAL_CONTROL_ENABLE
  gimbal_control_process();
#endif
  uint32_t now = HAL_GetTick();
  /* 前期以 100 Hz 更新电流采样，不在此处运行高速电流环。 */
  if ((int32_t)(now - next_sample) >= 0) {
    current_sample_update();
    next_sample = now + 10U;
  }
}
