#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

#include <stdbool.h>
#include <stdint.h>

/* 逻辑云台轴。物理接口映射集中在 motor_pwm.c 中维护。 */
typedef enum {
  GIMBAL_AXIS_X = 0,
  GIMBAL_AXIS_Y
} gimbal_axis_t;

void motor_pwm_init(void);
bool motor_pwm_set_enabled(bool enabled);
bool motor_pwm_is_enabled(void);

/* 低功率六步换相测试，到期或故障后自动关闭输出。 */
bool motor_pwm_test(gimbal_axis_t axis);

/* 低速闭环定位，目标值和返回值范围均为 0~4095。 */
bool motor_pwm_set_position(gimbal_axis_t axis, uint16_t target_raw,
                            uint16_t *final_raw);

#endif
