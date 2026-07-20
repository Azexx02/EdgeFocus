#ifndef MOTOR_PWM_H
#define MOTOR_PWM_H

#include <stdbool.h>
#include <stdint.h>

#include "gimbal_nv.h"

/* 逻辑云台轴。物理接口映射集中在 motor_pwm.c 中维护。 */
typedef enum {
  GIMBAL_AXIS_P = 0,
  GIMBAL_AXIS_Y
} gimbal_axis_t;

/* Legacy implementation alias. New protocol and application code use P/Y. */
#define GIMBAL_AXIS_X GIMBAL_AXIS_P

typedef enum {
  GIMBAL_LIMIT_NONE = 0,
  GIMBAL_LIMIT_X_MIN,
  GIMBAL_LIMIT_X_MAX,
  GIMBAL_LIMIT_X_BLOCKED
} gimbal_limit_event_t;

typedef struct {
  bool homed;
  uint16_t low_raw;
  uint16_t high_raw;
  uint16_t center_raw;
  uint16_t travel_deg;
  uint16_t center_deg;
} gimbal_axis_home_t;

typedef struct {
  bool enabled;
  bool x_aligned;
  bool y_aligned;
  uint16_t x_error_raw;
  uint16_t y_error_raw;
  uint16_t x_duty_ticks;
  uint16_t y_duty_ticks;
  int8_t x_direction;
  int8_t y_direction;
  uint8_t x_electric_offset;
  uint8_t y_electric_offset;
  uint8_t x_pole_pairs;
  uint8_t y_pole_pairs;
  int16_t x_gravity_comp;
  uint32_t cycles;
  uint32_t watchdog_trips;
} gimbal_foc_status_t;

void motor_pwm_init(void);
bool motor_pwm_prepare_continuous_axes(void);
bool motor_pwm_apply_electrical_calibration(
    gimbal_axis_t axis, const GimbalAxisElectricalCalibration *calibration);
bool motor_pwm_get_electrical_calibration(
    gimbal_axis_t axis, GimbalAxisElectricalCalibration *calibration);
bool motor_pwm_set_enabled(bool enabled);
bool motor_pwm_is_enabled(void);

/* FOC 小角度往返测试，结束或故障后自动关闭输出。 */
bool motor_pwm_test(gimbal_axis_t axis);

/* 编码器位置闭环 FOC，目标值和返回值范围均为 0..4095。 */
bool motor_pwm_set_position(gimbal_axis_t axis, uint16_t target_raw,
                            uint16_t *final_raw);
uint16_t motor_pwm_get_position_error(gimbal_axis_t axis);
gimbal_limit_event_t motor_pwm_get_limit_event(void);
uint16_t motor_pwm_get_limit_event_raw(void);
uint16_t motor_pwm_read_raw(gimbal_axis_t axis);
uint16_t motor_pwm_get_limit_deg(gimbal_axis_t axis);
void motor_pwm_set_home(gimbal_axis_t axis, uint16_t raw);
uint16_t motor_pwm_get_home(gimbal_axis_t axis);
bool motor_pwm_home_from_encoder(gimbal_axis_t axis, uint16_t *home_raw);
bool motor_pwm_raw_to_deg(gimbal_axis_t axis, uint16_t raw, uint16_t *deg);
bool motor_pwm_deg_to_raw(gimbal_axis_t axis, uint16_t deg, uint16_t *raw);
bool motor_pwm_set_position_deg(gimbal_axis_t axis, uint16_t target_deg,
                                uint16_t *final_deg, uint16_t *final_raw);
void motor_pwm_select_position_power(gimbal_axis_t axis,
                                     int8_t mechanical_dir);
void motor_pwm_restore_idle_power(void);
bool motor_pwm_auto_home_axis(gimbal_axis_t axis, gimbal_axis_home_t *home);
bool motor_pwm_auto_home_all(void);
bool motor_pwm_mark_manual_limit(gimbal_axis_t axis, bool low_end,
                                 uint16_t *raw);
bool motor_pwm_set_manual_limit_raw(gimbal_axis_t axis, bool low_end,
                                    uint16_t raw);
bool motor_pwm_move_to_marked_center(gimbal_axis_t axis, uint16_t *final_raw);
uint16_t motor_pwm_get_home_error(gimbal_axis_t axis);
bool motor_pwm_is_homed(gimbal_axis_t axis);
bool motor_pwm_calibration_is_valid(gimbal_axis_t axis);
bool motor_pwm_get_home_info(gimbal_axis_t axis, gimbal_axis_home_t *home);
void motor_pwm_clear_calibration(void);
void motor_pwm_hold_enable(bool enabled);
bool motor_pwm_hold_is_enabled(void);
bool motor_pwm_hold_set_target(gimbal_axis_t axis, uint16_t target_deg);
bool motor_pwm_hold_set_target_tenths(gimbal_axis_t axis,
                                      uint16_t target_tenths);
uint16_t motor_pwm_hold_get_target(gimbal_axis_t axis);
void motor_pwm_hold_process(void);
void motor_pwm_hold_watchdog(void);
void motor_pwm_get_foc_status(gimbal_foc_status_t *status);
void motor_pwm_gravity_clear(void);

#endif
