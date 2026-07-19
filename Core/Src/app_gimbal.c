#include "app_gimbal.h"
#include "board_config.h"
#include "current_sample.h"
#include "gimbal_config.h"
#include "main.h"
#include "motor_pwm.h"
#include "soft_i2c.h"
#include "uart_protocol.h"
#include <stdio.h>

static bool enable_input_was_high;
static bool uart_armed;

static void send_reset_cause(void)
{
  char response[80];
  snprintf(response, sizeof(response),
           "BOOT,RESET=BOR:%u,PIN:%u,SOFT:%u,IWDG:%u,WWDG:%u\r\n",
           __HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) ? 1U : 0U);
  uart_protocol_send(response);
  __HAL_RCC_CLEAR_RESET_FLAGS();
}

static bool align_gimbal(void)
{
#if GIMBAL_X_CONTINUOUS && GIMBAL_Y_CONTINUOUS
  uart_protocol_send("BOOT_ALIGN,START\r\n");
  if (motor_pwm_prepare_continuous_axes()) {
    board_set_fault(false);
    uart_protocol_send("BOOT_ALIGN,OK\r\n");
    return true;
  }

  motor_pwm_hold_enable(false);
  board_set_fault(true);
  uart_protocol_send("ERR,BOOT_ALIGN\r\n");
  return false;
#else
  board_set_fault(false);
  return true;
#endif
}

bool app_gimbal_arm(void)
{
  uart_armed = false;
  motor_pwm_hold_enable(false);
  (void)motor_pwm_set_enabled(false);
  board_set_fault(false);
  uart_armed = align_gimbal();
  return uart_armed;
}

void app_gimbal_disarm(void)
{
  uart_armed = false;
  motor_pwm_hold_enable(false);
  (void)motor_pwm_set_enabled(false);
  board_set_boost(false);
  board_set_fault(false);
}

bool app_gimbal_is_armed(void) { return uart_armed; }

void app_gimbal_init(void)
{
  /* Keep every power output disabled until all software modules are ready. */
  board_config_init();
  soft_i2c_init();
  motor_pwm_init();
  current_sample_init();
  uart_protocol_init();
  send_reset_cause();
  enable_input_was_high = board_get_enable_input();
  uart_armed = false;

  {
    char response[64];
    snprintf(response, sizeof(response), "BOOT,ENABLE_IN=%u,BOOT0=%u\r\n",
             board_get_enable_input() ? 1U : 0U,
             board_get_boot0_ctrl() ? 1U : 0U);
    uart_protocol_send(response);
  }

#if GIMBAL_UART_ONLY_MODE
  board_set_fault(false);
  uart_protocol_send("BOOT,MODE=UART_ONLY,READY\r\n");
#else
  if (enable_input_was_high) {
    (void)align_gimbal();
  } else {
    /* A disabled product board is a normal, fault-free standby state. */
    board_set_fault(false);
    uart_protocol_send("BOOT_WAIT_ENABLE\r\n");
  }
#endif

}

void app_gimbal_process(void)
{
  static uint32_t next_sample;
  uint32_t now;

#if GIMBAL_REQUIRE_ENABLE_INPUT
  bool enable_input_high = board_get_enable_input();
  if (enable_input_was_high && !enable_input_high) {
    bool interrupted_motion = motor_pwm_is_enabled() ||
                              motor_pwm_hold_is_enabled() ||
                              board_get_boost();
    motor_pwm_hold_enable(false);
    (void)motor_pwm_set_enabled(false);
    if (interrupted_motion) {
      board_set_fault(true);
      uart_protocol_send("ERR,ENABLE_IN_LOST\r\n");
    } else {
      board_set_fault(false);
      uart_protocol_send("ENABLE,OFF\r\n");
    }
  } else if (!enable_input_was_high && enable_input_high) {
    /* Product boards without NRST can arm and re-align on this edge. */
    (void)align_gimbal();
  }
  enable_input_was_high = enable_input_high;
#endif

  motor_pwm_hold_process();
  uart_protocol_process();
  now = HAL_GetTick();
  if ((int32_t)(now - next_sample) >= 0) {
    current_sample_update();
    next_sample = now + 10U;
  }
}
