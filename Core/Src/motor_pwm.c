#include "motor_pwm.h"
#include "as5600.h"
#include "board_config.h"
#include "current_sample.h"
#include "gimbal_config.h"
#include "main.h"
#include "tim.h"
#include "uart_protocol.h"
#include "usart.h"
#include <math.h>

/* TIM1/TIM8 的自动重装值为 4249，250 对应约 5.9% 占空比。 */
#define TEST_DUTY_TICKS          250U
#define POSITION_DEADBAND         12
#define POSITION_MAX_STEPS        80U
#define POSITION_SEGMENT_DEG      10U
#define POSITION_GUARD_MARGIN_DEG 20U

#define SINE_STEPS 24U
#define MOTOR_POLE_PAIRS 7U
#define FOC_TWO_PI 6.28318530718f
#define FOC_HALF_PI 1.57079632679f
#define HOME_ERR_NONE 0U
#define HOME_ERR_START 1U
#define HOME_ERR_CAL 2U
#define HOME_ERR_LOW_STOP 3U
#define HOME_ERR_HIGH_STOP 4U
#define HOME_ERR_TRAVEL 5U
#define HOME_ERR_CENTER 6U
#define ALL_PHASE_OUTPUTS (TIM_CCER_CC1E | TIM_CCER_CC1NE | \
                           TIM_CCER_CC2E | TIM_CCER_CC2NE | \
                           TIM_CCER_CC3E | TIM_CCER_CC3NE)

typedef uint16_t (*encoder_reader_t)(void);

typedef struct {
  TIM_HandleTypeDef *timer;
  GPIO_TypeDef *en_port;
  uint16_t en_pin;
  GPIO_TypeDef *stby_port;
  uint16_t stby_pin;
  encoder_reader_t read_encoder;
} axis_hardware_t;

static bool all_motors_enabled;
static uint16_t axis_home_raw[2];
static bool position_guard_enabled;
static uint16_t position_guard_min_deg;
static uint16_t position_guard_max_deg;
static uint32_t active_duty_ticks = TEST_DUTY_TICKS;
static gimbal_axis_home_t axis_home_info[2];
static uint16_t axis_home_error[2];
static uint16_t axis_position_error[2];
static gimbal_limit_event_t limit_event;
static uint16_t limit_event_raw = AS5600_READ_ERROR;
static bool manual_low_valid[2];
static bool manual_high_valid[2];
static volatile bool hold_enabled;
static uint16_t hold_target_deg[2];
static uint16_t hold_target_raw_command[2];
static float hold_target_raw_filtered[2];
static bool hold_commutation_ready[2];
static uint8_t hold_commutation[2];
static int8_t hold_forward_raw_sign[2];
static int8_t hold_torque_polarity[2];
static float hold_electric_offset[2];
static uint8_t hold_pole_pairs[2] = {MOTOR_POLE_PAIRS, MOTOR_POLE_PAIRS};
static uint16_t hold_lost_count[2];
static uint16_t hold_error_raw[2];
static uint16_t hold_previous_raw[2];
static uint16_t hold_last_encoder_raw[2];
static float hold_control_raw_filtered[2];
static float hold_speed_filtered[2];
static float hold_integral_effort[2];
static uint16_t hold_integral_qualify_count[2];
static uint16_t hold_settle_count[2];
static bool hold_settled[2];
static uint8_t hold_encoder_error_count[2];
static uint16_t hold_duty_ticks[2];
static int8_t hold_torque_sign[2];
static uint8_t hold_reverse_neutral_count[2];
static uint8_t hold_field_step[2];
static uint16_t hold_startup_cycles;
static volatile uint32_t hold_cycle_count;
static volatile uint32_t hold_watchdog_trips;
static uint32_t next_hold_tick;
static volatile uint32_t hold_watchdog_deadline;

static uint16_t read_encoder_quiet(const axis_hardware_t *hardware);
static uint16_t read_encoder_live(const axis_hardware_t *hardware);
static uint16_t axis_current_raw(gimbal_axis_t axis);
static bool align_hold_electrical_angle(gimbal_axis_t axis);

static bool emergency_stop_requested(void)
{
  uart_protocol_process();
  return false;
}

/*
 * 云台轴与 MCU 资源映射：
 * X 轴：编码器 PA15/PB7（I2C1）；电机 TIM8，PA0 使能，PC1 待机。
 * Y 轴：编码器 PB10/PB11（软件 I2C）；电机 TIM1，PA6 使能，PC0 待机。
 */
static axis_hardware_t get_axis_hardware(gimbal_axis_t axis)
{
  axis_hardware_t hardware;
  if (axis == GIMBAL_AXIS_X) {
    hardware.timer = &htim1;
    hardware.en_port = GIMBAL_Y_EN_GPIO_Port;
    hardware.en_pin = GIMBAL_Y_EN_Pin;
    hardware.stby_port = GIMBAL_Y_STBY_GPIO_Port;
    hardware.stby_pin = GIMBAL_Y_STBY_Pin;
    hardware.read_encoder = as5600_x_read_raw;
  } else {
    hardware.timer = &htim8;
    hardware.en_port = GIMBAL_X_EN_GPIO_Port;
    hardware.en_pin = GIMBAL_X_EN_Pin;
    hardware.stby_port = GIMBAL_X_STBY_GPIO_Port;
    hardware.stby_pin = GIMBAL_X_STBY_Pin;
    hardware.read_encoder = as5600_y_read_raw;
  }
  return hardware;
}

static HAL_StatusTypeDef start_timer(TIM_HandleTypeDef *timer)
{
  if ((timer->Instance->CR1 & TIM_CR1_CEN) != 0U) {
    timer->Instance->CCER |= ALL_PHASE_OUTPUTS;
    return HAL_OK;
  }

  for (uint32_t channel = TIM_CHANNEL_1; channel <= TIM_CHANNEL_3; channel += 4U) {
    if (HAL_TIM_PWM_Start(timer, channel) != HAL_OK) return HAL_ERROR;
    if (HAL_TIMEx_PWMN_Start(timer, channel) != HAL_OK) return HAL_ERROR;
  }
  return HAL_OK;
}

static void set_all_compare(TIM_HandleTypeDef *timer, uint32_t value)
{
  __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_1, value);
  __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_2, value);
  __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_3, value);
}

/* 一相高侧 PWM、一相低侧导通、第三相悬空。 */
static float foc_wrap_angle(float angle)
{
  while (angle >= FOC_TWO_PI) angle -= FOC_TWO_PI;
  while (angle < 0.0f) angle += FOC_TWO_PI;
  return angle;
}

static uint32_t foc_phase_compare(TIM_HandleTypeDef *timer, float phase,
                                  uint32_t amplitude)
{
  float center = (float)timer->Init.Period * 0.5f;
  float value = center + sinf(phase) * (float)amplitude;
  if (value < 1.0f) value = 1.0f;
  if (value >= (float)timer->Init.Period) {
    value = (float)timer->Init.Period - 1.0f;
  }
  return (uint32_t)value;
}

/*
 * Continuous inverse Park/Clarke voltage vector.  This replaces the old
 * 24-step electrical lookup, whose 15-degree quantisation was a primary
 * source of audible chatter and position jitter.
 */
static void update_foc_vector(TIM_HandleTypeDef *timer, float field_angle,
                              uint32_t amplitude)
{
  field_angle = foc_wrap_angle(field_angle);
  __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_1,
                        foc_phase_compare(timer, field_angle, amplitude));
  __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_2,
                        foc_phase_compare(timer,
                                          field_angle - 2.09439510239f,
                                          amplitude));
  __HAL_TIM_SET_COMPARE(timer, TIM_CHANNEL_3,
                        foc_phase_compare(timer,
                                          field_angle + 2.09439510239f,
                                          amplitude));
}

static float axis_electrical_angle(gimbal_axis_t axis, uint16_t raw)
{
  float shaft = (float)raw * (FOC_TWO_PI / 4096.0f);
  return foc_wrap_angle((float)hold_forward_raw_sign[axis] *
                        (float)hold_pole_pairs[axis] * shaft +
                        hold_electric_offset[axis]);
}

static void stop_axis(const axis_hardware_t *hardware)
{
  hardware->timer->Instance->CCER &= ~ALL_PHASE_OUTPUTS;
  set_all_compare(hardware->timer, 0U);
  HAL_GPIO_WritePin(hardware->en_port, hardware->en_pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(hardware->stby_port, hardware->stby_pin, GPIO_PIN_RESET);
}

static bool start_axis(const axis_hardware_t *hardware)
{
  set_all_compare(hardware->timer, 0U);
  if (start_timer(hardware->timer) != HAL_OK) return false;
  HAL_GPIO_WritePin(hardware->stby_port, hardware->stby_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(hardware->en_port, hardware->en_pin, GPIO_PIN_SET);
  HAL_Delay(5U);

  /* EN/FAULT 为开漏复用脚，仍为低表示驱动器正在报告故障。 */
  if (HAL_GPIO_ReadPin(hardware->en_port, hardware->en_pin) == GPIO_PIN_RESET) {
    stop_axis(hardware);
    return false;
  }
  return true;
}

static int16_t angle_error(uint16_t target, uint16_t current)
{
  int32_t error = (int32_t)target - (int32_t)current;
  if (error > 2047) error -= 4096;
  if (error < -2048) error += 4096;
  return (int16_t)error;
}

static uint16_t axis_limit_deg(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_LIMIT_DEG : GIMBAL_Y_LIMIT_DEG;
}

static int8_t axis_positive_raw_sign(gimbal_axis_t axis)
{
  return (axis == GIMBAL_AXIS_X ? GIMBAL_X_POSITIVE_RAW_SIGN :
          GIMBAL_Y_POSITIVE_RAW_SIGN) >= 0 ? 1 : -1;
}

static bool axis_is_continuous(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? (GIMBAL_X_CONTINUOUS != 0) :
                                 (GIMBAL_Y_CONTINUOUS != 0);
}

static void initialize_continuous_axis(gimbal_axis_t axis)
{
  uint16_t raw = axis_current_raw(axis);
  if (!axis_is_continuous(axis) || raw == AS5600_READ_ERROR) return;

  axis_home_raw[axis] = raw;
  axis_home_info[axis].homed = true;
  axis_home_info[axis].low_raw = raw;
  axis_home_info[axis].high_raw =
      axis_positive_raw_sign(axis) > 0 ?
      (uint16_t)((raw + 4085U) & 0x0FFFU) :
      (uint16_t)((raw + 11U) & 0x0FFFU);
  axis_home_info[axis].center_raw =
      axis_positive_raw_sign(axis) > 0 ?
      (uint16_t)((raw + 2042U) & 0x0FFFU) :
      (uint16_t)((raw + 4096U - 2042U) & 0x0FFFU);
  axis_home_info[axis].travel_deg = axis_limit_deg(axis);
  axis_home_info[axis].center_deg = axis_limit_deg(axis) / 2U;
  axis_home_error[axis] = HOME_ERR_NONE;
  manual_low_valid[axis] = true;
  manual_high_valid[axis] = true;
  hold_target_deg[axis] = 0U;
  hold_target_raw_command[axis] = raw;
  hold_target_raw_filtered[axis] = (float)raw;
}

static uint16_t axis_current_raw(gimbal_axis_t axis)
{
  axis_hardware_t hardware = get_axis_hardware(axis);
  return hardware.read_encoder();
}

static bool current_limit_fault(void)
{
  if (GIMBAL_CURRENT_LIMIT_RAW >= 4095U) return false;
  if (!current_sample_update()) return false;
  return current_get_x_raw() > GIMBAL_CURRENT_LIMIT_RAW ||
         current_get_y_raw() > GIMBAL_CURRENT_LIMIT_RAW;
}

static bool raw_to_limited_deg(gimbal_axis_t axis, uint16_t raw, uint16_t *deg)
{
  if (raw > 4095U || deg == NULL) return false;

  uint16_t travel_counts = axis_positive_raw_sign(axis) > 0 ?
      (uint16_t)((axis_home_info[axis].high_raw + 4096U -
                  axis_home_info[axis].low_raw) & 0x0FFFU) :
      (uint16_t)((axis_home_info[axis].low_raw + 4096U -
                  axis_home_info[axis].high_raw) & 0x0FFFU);
  if (travel_counts < 128U || axis_home_info[axis].travel_deg == 0U) {
    return false;
  }
  uint32_t delta = axis_positive_raw_sign(axis) > 0 ?
                   (((uint32_t)raw + 4096U - axis_home_raw[axis]) & 0x0FFFU) :
                   (((uint32_t)axis_home_raw[axis] + 4096U - raw) & 0x0FFFU);
  if (delta > 4096U - POSITION_DEADBAND) {
    delta = 0U;
  }

  if (delta > travel_counts + POSITION_DEADBAND) return false;
  if (delta > travel_counts) delta = travel_counts;
  uint32_t value =
      (delta * axis_home_info[axis].travel_deg + travel_counts / 2U) /
      travel_counts;
  if (value > axis_home_info[axis].travel_deg) return false;

  *deg = (uint16_t)value;
  return true;
}

static bool position_guard_allows(gimbal_axis_t axis, uint16_t raw)
{
  if (!position_guard_enabled) return true;

  uint16_t deg;
  if (!raw_to_limited_deg(axis, raw, &deg)) return false;
  return deg >= position_guard_min_deg && deg <= position_guard_max_deg;
}

static int8_t axis_torque_polarity(gimbal_axis_t axis)
{
  return hold_torque_polarity[axis] >= 0 ? 1 : -1;
}

static void latch_x_limit_event(uint16_t raw)
{
  uint16_t deg;
  limit_event_raw = raw;
  if (!raw_to_limited_deg(GIMBAL_AXIS_X, raw, &deg)) {
    limit_event = GIMBAL_LIMIT_X_BLOCKED;
  } else if (deg <= GIMBAL_X_LIMIT_ZONE_DEG) {
    limit_event = GIMBAL_LIMIT_X_MIN;
  } else if (deg >= GIMBAL_X_LIMIT_DEG - GIMBAL_X_LIMIT_ZONE_DEG) {
    limit_event = GIMBAL_LIMIT_X_MAX;
  } else {
    limit_event = GIMBAL_LIMIT_X_BLOCKED;
  }
}

static uint16_t axis_max_homing_steps(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOMING_MAX_STEPS :
         GIMBAL_Y_HOMING_MAX_STEPS;
}

static uint32_t axis_homing_duty(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOMING_DUTY_TICKS :
         GIMBAL_Y_HOMING_DUTY_TICKS;
}

static uint32_t axis_direction_homing_duty(gimbal_axis_t axis,
                                           int8_t mechanical_dir)
{
  if (axis == GIMBAL_AXIS_X && mechanical_dir == GIMBAL_X_CLOCKWISE_MECH_DIR) {
    return GIMBAL_X_CLOCKWISE_HOMING_DUTY_TICKS;
  }
  return axis_homing_duty(axis);
}

static uint16_t axis_direction_homing_dac(gimbal_axis_t axis,
                                          int8_t mechanical_dir,
                                          uint16_t fallback_mv)
{
  if (axis == GIMBAL_AXIS_X && mechanical_dir == GIMBAL_X_CLOCKWISE_MECH_DIR) {
    return GIMBAL_X_CLOCKWISE_HOMING_DAC_MV;
  }
  (void)axis;
  return fallback_mv;
}

static uint32_t axis_center_duty(gimbal_axis_t axis, int8_t mechanical_dir)
{
  if (axis == GIMBAL_AXIS_X && mechanical_dir == GIMBAL_X_CLOCKWISE_MECH_DIR) {
    return GIMBAL_X_CLOCKWISE_CENTER_DUTY_TICKS;
  }
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_CENTER_DUTY_TICKS :
         GIMBAL_Y_CENTER_DUTY_TICKS;
}

static uint32_t axis_hold_duty(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOLD_DUTY_TICKS :
         GIMBAL_Y_HOLD_DUTY_TICKS;
}

static uint32_t axis_hold_min_duty(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOLD_MIN_DUTY_TICKS :
         GIMBAL_Y_HOLD_MIN_DUTY_TICKS;
}

static uint16_t axis_center_dac(gimbal_axis_t axis, int8_t mechanical_dir)
{
  if (axis == GIMBAL_AXIS_X && mechanical_dir == GIMBAL_X_CLOCKWISE_MECH_DIR) {
    return GIMBAL_X_CLOCKWISE_CENTER_DAC_MV;
  }
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_CENTER_DAC_MV :
         GIMBAL_Y_CENTER_DAC_MV;
}

static void set_axis_homing_dac_mv(gimbal_axis_t axis, uint16_t mv)
{
  if (axis == GIMBAL_AXIS_X) {
    board_set_dac_mv(mv, GIMBAL_IDLE_DAC_MV);
  } else {
    board_set_dac_mv(GIMBAL_IDLE_DAC_MV, mv);
  }
}

static uint32_t axis_homing_duty_max(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOMING_DUTY_MAX_TICKS :
         GIMBAL_Y_HOLD_DUTY_TICKS;
}

static uint32_t axis_homing_duty_step(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOMING_DUTY_STEP_TICKS : 0U;
}

static uint16_t axis_homing_dac_max(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOMING_DAC_MAX_MV :
         GIMBAL_Y_HOMING_DAC_MV;
}

static uint16_t axis_homing_dac_step(gimbal_axis_t axis)
{
  return axis == GIMBAL_AXIS_X ? GIMBAL_X_HOMING_DAC_STEP_MV : 0U;
}

static bool increase_axis_homing_power(gimbal_axis_t axis, uint16_t *dac_mv)
{
  bool increased = false;
  uint32_t duty_step = axis_homing_duty_step(axis);
  uint32_t duty_max = axis_homing_duty_max(axis);
  if (duty_step != 0U && active_duty_ticks < duty_max) {
    active_duty_ticks += duty_step;
    if (active_duty_ticks > duty_max) active_duty_ticks = duty_max;
    increased = true;
  }

  uint16_t dac_step = axis_homing_dac_step(axis);
  uint16_t dac_max = axis_homing_dac_max(axis);
  if (dac_step != 0U && *dac_mv < dac_max) {
    uint32_t next = (uint32_t)*dac_mv + dac_step;
    *dac_mv = next > dac_max ? dac_max : (uint16_t)next;
    set_axis_homing_dac_mv(axis, *dac_mv);
    increased = true;
  }
  return increased;
}

static bool axis_homing_power_at_max(gimbal_axis_t axis,
                                     const uint16_t *dac_mv)
{
  return active_duty_ticks >= axis_homing_duty_max(axis) &&
         (dac_mv == NULL || *dac_mv >= axis_homing_dac_max(axis));
}

static void restore_idle_dac(void)
{
  board_set_dac_mv(GIMBAL_IDLE_DAC_MV, GIMBAL_IDLE_DAC_MV);
}

static uint16_t axis_min_homing_counts(gimbal_axis_t axis)
{
  return (uint16_t)(((uint32_t)axis_limit_deg(axis) * 4096U * 60U) /
                    (360U * 100U));
}

static uint16_t first_stop_min_counts(void)
{
  return (uint16_t)(((uint32_t)GIMBAL_HOMING_FIRST_STOP_MIN_DEG * 4096U) /
                    360U);
}

static uint16_t raw_delta_abs(uint16_t a, uint16_t b)
{
  int16_t delta = angle_error(a, b);
  return delta < 0 ? (uint16_t)-delta : (uint16_t)delta;
}

static uint16_t axis_min_calibration_deg(gimbal_axis_t axis)
{
  return (uint16_t)((axis_limit_deg(axis) * 60U) / 100U);
}

static uint32_t slew_u32(uint32_t current, uint32_t target, uint32_t step)
{
  if (target > current) {
    uint32_t delta = target - current;
    return current + (delta > step ? step : delta);
  }
  uint32_t delta = current - target;
  return current - (delta > step ? step : delta);
}

static void foc_apply_raw_target(gimbal_axis_t axis,
                                 const axis_hardware_t *hardware,
                                 uint16_t current_raw,
                                 uint16_t target_raw)
{
  int16_t error = angle_error(target_raw, current_raw);
  uint16_t error_abs = error < 0 ? (uint16_t)-error : (uint16_t)error;
  /*
   * Use the caller-selected ceiling.  The previous implementation always
   * started at homing power, so even a one-count position correction could
   * receive near-full bridge voltage and overshoot badly.
   */
  uint32_t duty_min = axis_hold_min_duty(axis);
  uint32_t duty_max = active_duty_ticks;
  if (duty_max < duty_min) duty_max = duty_min;
  if (duty_max > axis_homing_duty_max(axis)) {
    duty_max = axis_homing_duty_max(axis);
  }
  uint32_t scaled = error_abs;
  if (scaled > GIMBAL_FOC_FULL_TORQUE_ERROR_RAW) {
    scaled = GIMBAL_FOC_FULL_TORQUE_ERROR_RAW;
  }
  active_duty_ticks = duty_min +
      ((duty_max - duty_min) * scaled) / GIMBAL_FOC_FULL_TORQUE_ERROR_RAW;

  float field_angle = axis_electrical_angle(axis, current_raw);
  if (error_abs > GIMBAL_FOC_DEADBAND_RAW) {
    int8_t requested_raw_sign =
        (int8_t)((error >= 0 ? 1 : -1) * axis_torque_polarity(axis));
    bool advance_field = requested_raw_sign == hold_forward_raw_sign[axis];
    field_angle += advance_field ? FOC_HALF_PI : -FOC_HALF_PI;
  }
  update_foc_vector(hardware->timer, field_angle, active_duty_ticks);
}

static void foc_apply_position_pd(gimbal_axis_t axis,
                                  const axis_hardware_t *hardware,
                                  uint16_t current_raw,
                                  uint16_t target_raw,
                                  uint16_t previous_raw)
{
  int32_t error = angle_error(target_raw, current_raw);
  int32_t speed = angle_error(current_raw, previous_raw);
  int32_t kp = axis == GIMBAL_AXIS_X ?
               GIMBAL_X_POSITION_KP : GIMBAL_Y_POSITION_KP;
  int32_t kd = axis == GIMBAL_AXIS_X ?
               GIMBAL_X_POSITION_KD : GIMBAL_Y_POSITION_KD;
  int32_t effort = error * kp - speed * kd;
  uint32_t static_duty = axis == GIMBAL_AXIS_X ?
                         GIMBAL_X_POSITION_STATIC_DUTY :
                         GIMBAL_Y_POSITION_STATIC_DUTY;
  uint32_t error_abs = error < 0 ? (uint32_t)-error : (uint32_t)error;
  uint32_t amplitude;

  if (error >= -POSITION_DEADBAND && error <= POSITION_DEADBAND) {
    effort = 0;
  }
  if (effort > (int32_t)active_duty_ticks) {
    effort = (int32_t)active_duty_ticks;
  } else if (effort < -(int32_t)active_duty_ticks) {
    effort = -(int32_t)active_duty_ticks;
  }
  amplitude = effort < 0 ? (uint32_t)-effort : (uint32_t)effort;
  if (effort != 0) {
    /*
     * Fade friction feed-forward near the target.  Applying the full static
     * kick after a tiny overshoot caused X to cross the target repeatedly.
     */
    if (error_abs < GIMBAL_POSITION_STATIC_RAMP_RAW) {
      static_duty = (static_duty * error_abs) /
                    GIMBAL_POSITION_STATIC_RAMP_RAW;
    }
    amplitude += static_duty;
    if (axis == GIMBAL_AXIS_Y && effort < 0) {
      uint32_t direction_comp = GIMBAL_Y_NEGATIVE_DUTY_COMP;
      if (error_abs < GIMBAL_POSITION_STATIC_RAMP_RAW) {
        direction_comp = (direction_comp * error_abs) /
                         GIMBAL_POSITION_STATIC_RAMP_RAW;
      }
      amplitude += direction_comp;
    }
    if (amplitude > active_duty_ticks) amplitude = active_duty_ticks;
  }

  float field_angle = axis_electrical_angle(axis, current_raw);
  if (effort != 0) {
    int8_t requested_raw_sign =
        (int8_t)((effort > 0 ? 1 : -1) * axis_torque_polarity(axis));
    bool advance_field = requested_raw_sign == hold_forward_raw_sign[axis];
    field_angle += advance_field ? FOC_HALF_PI : -FOC_HALF_PI;
  }
  update_foc_vector(hardware->timer, field_angle, amplitude);
}

static bool scan_to_mechanical_stop(gimbal_axis_t axis,
                                    const axis_hardware_t *hardware,
                                    int8_t mechanical_dir,
                                    bool first_stop,
                                    uint16_t *stop_raw,
                                    uint16_t *travel_counts,
                                    uint16_t *dac_mv)
{
  uint16_t previous = read_encoder_quiet(hardware);
  if (previous == AS5600_READ_ERROR) return false;

  int8_t desired_raw_sign = (int8_t)(mechanical_dir *
                                    axis_positive_raw_sign(axis));
  uint16_t target_raw = previous;
  uint16_t stall_count = 0U;
  uint8_t window_count = 0U;
  uint16_t window_start = previous;
  int32_t progress_counts = 0;
  uint32_t scan_start_tick = HAL_GetTick();

  uint32_t direction_duty = axis_direction_homing_duty(axis, mechanical_dir);
  if (active_duty_ticks < direction_duty) {
    active_duty_ticks = direction_duty;
  }
  if (dac_mv != NULL) {
    uint16_t direction_dac = axis_direction_homing_dac(axis,
                                                      mechanical_dir,
                                                      *dac_mv);
    if (*dac_mv < direction_dac) {
      *dac_mv = direction_dac;
      set_axis_homing_dac_mv(axis, *dac_mv);
    }
  }

  for (uint16_t step = 0U; step < axis_max_homing_steps(axis); ++step) {
    if (emergency_stop_requested()) break;
    if ((HAL_GetTick() - scan_start_tick) >=
        GIMBAL_HOMING_SCAN_TIMEOUT_MS) {
      break;
    }
    int16_t target_lead = angle_error(target_raw, previous);
    uint16_t lead_abs = target_lead < 0 ?
                        (uint16_t)-target_lead : (uint16_t)target_lead;
    if (lead_abs < GIMBAL_HOMING_FOC_MAX_LEAD_RAW) {
      target_raw = desired_raw_sign > 0 ?
                   (uint16_t)((target_raw + GIMBAL_HOMING_FOC_STEP_RAW) &
                              0x0FFFU) :
                   (uint16_t)((target_raw + 4096U -
                               GIMBAL_HOMING_FOC_STEP_RAW) & 0x0FFFU);
    }
    foc_apply_raw_target(axis, hardware, previous, target_raw);
    HAL_Delay(GIMBAL_FOC_PERIOD_MS);

    if (HAL_GPIO_ReadPin(hardware->en_port, hardware->en_pin) == GPIO_PIN_RESET ||
        current_limit_fault()) {
      return false;
    }

    uint16_t current = read_encoder_quiet(hardware);
    if (current == AS5600_READ_ERROR) return false;

    uint16_t moved = raw_delta_abs(current, previous);
    uint16_t window_moved = raw_delta_abs(current, window_start);
    if (moved <= GIMBAL_HOMING_STALL_RAW) {
      if (step > 12U && ++stall_count >= GIMBAL_HOMING_STALL_COUNT) {
        bool enough_progress =
            (first_stop && progress_counts >= first_stop_min_counts()) ||
            (!first_stop && progress_counts >= axis_min_homing_counts(axis));
        if (enough_progress && axis_homing_power_at_max(axis, dac_mv)) {
          *stop_raw = current;
          if (travel_counts != NULL) {
            *travel_counts = (uint16_t)progress_counts;
          }
          return true;
        }
        if (dac_mv != NULL && increase_axis_homing_power(axis, dac_mv)) {
          stall_count = 0U;
          window_count = 0U;
          window_start = current;
          continue;
        }
        /*
         * The axis may already be sitting on the first requested endpoint.
         * Once maximum homing power has been tried, return promptly so the
         * caller can probe the opposite endpoint instead of timing out.
         */
        if (first_stop && progress_counts < first_stop_min_counts() &&
            axis_homing_power_at_max(axis, dac_mv) &&
            stall_count >= (uint16_t)(GIMBAL_HOMING_STALL_COUNT * 3U)) {
          *stop_raw = current;
          if (travel_counts != NULL) {
            *travel_counts = (uint16_t)progress_counts;
          }
          return false;
        }
      }
    } else {
      stall_count = 0U;
      int16_t raw_delta = angle_error(current, previous);
      int16_t mechanical_delta = (int16_t)(raw_delta *
                                          axis_positive_raw_sign(axis) *
                                          mechanical_dir);
      /*
       * Keep homing strictly one-way. Wiring spring-back and endpoint jitter
       * may briefly move the encoder backwards; ignore that displacement
       * instead of rolling the electrical phase back and probing repeatedly.
       */
      if (mechanical_delta > 0) {
        progress_counts += mechanical_delta;
      }
      if (progress_counts < 0) progress_counts = 0;
      if (progress_counts > 4095) progress_counts = 4095;
      previous = current;
    }

    if (++window_count >= GIMBAL_HOMING_WINDOW_STEPS) {
      if (step > 24U && window_moved <= GIMBAL_HOMING_WINDOW_RAW) {
        bool enough_progress =
            (first_stop && progress_counts >= first_stop_min_counts()) ||
            (!first_stop && progress_counts >= axis_min_homing_counts(axis));
        if (enough_progress && axis_homing_power_at_max(axis, dac_mv)) {
          *stop_raw = current;
          if (travel_counts != NULL) {
            *travel_counts = (uint16_t)progress_counts;
          }
          return true;
        }
        if (dac_mv != NULL && increase_axis_homing_power(axis, dac_mv)) {
          window_count = 0U;
          window_start = current;
          continue;
        }
      }
      window_count = 0U;
      window_start = current;
    }
  }

  *stop_raw = previous;
  if (travel_counts != NULL) *travel_counts = (uint16_t)progress_counts;
  return false;
}

static bool move_to_raw_in_direction(gimbal_axis_t axis,
                                     const axis_hardware_t *hardware,
                                     int8_t mechanical_dir,
                                     uint16_t target_raw,
                                     uint16_t max_steps,
                                     uint16_t *final_raw)
{
  int8_t desired_raw_sign = (int8_t)(mechanical_dir *
                                    axis_positive_raw_sign(axis));
  uint16_t commanded_raw = read_encoder_quiet(hardware);
  if (commanded_raw == AS5600_READ_ERROR) return false;

  for (uint16_t step = 0U; step < max_steps; ++step) {
    if (emergency_stop_requested()) return false;
    uint16_t before = read_encoder_quiet(hardware);
    if (before == AS5600_READ_ERROR) return false;
    int16_t error = angle_error(target_raw, before);
    if (error >= -POSITION_DEADBAND && error <= POSITION_DEADBAND) {
      if (final_raw != NULL) *final_raw = before;
      return true;
    }

    int16_t command_error = angle_error(target_raw, commanded_raw);
    if (command_error >= -(int16_t)GIMBAL_HOMING_FOC_STEP_RAW &&
        command_error <= (int16_t)GIMBAL_HOMING_FOC_STEP_RAW) {
      commanded_raw = target_raw;
    } else {
      commanded_raw = desired_raw_sign > 0 ?
                      (uint16_t)((commanded_raw +
                                  GIMBAL_HOMING_FOC_STEP_RAW) & 0x0FFFU) :
                      (uint16_t)((commanded_raw + 4096U -
                                  GIMBAL_HOMING_FOC_STEP_RAW) & 0x0FFFU);
    }
    foc_apply_raw_target(axis, hardware, before, commanded_raw);
    HAL_Delay(GIMBAL_FOC_PERIOD_MS);

    if (HAL_GPIO_ReadPin(hardware->en_port, hardware->en_pin) == GPIO_PIN_RESET ||
        current_limit_fault()) {
      return false;
    }

    uint16_t current = read_encoder_quiet(hardware);
    if (current == AS5600_READ_ERROR) return false;
    int16_t after_error = angle_error(target_raw, current);
    if (after_error >= -POSITION_DEADBAND && after_error <= POSITION_DEADBAND) {
      if (final_raw != NULL) *final_raw = current;
      return true;
    }
  }

  if (final_raw != NULL) *final_raw = read_encoder_quiet(hardware);
  return false;
}

/* 暂停功率输出后读取编码器，降低 PWM 对 I2C 的干扰。 */
static uint16_t read_encoder_quiet(const axis_hardware_t *hardware)
{
  uint32_t outputs = hardware->timer->Instance->CCER & ALL_PHASE_OUTPUTS;
  hardware->timer->Instance->CCER &= ~ALL_PHASE_OUTPUTS;
  HAL_Delay(2U);

  uint16_t raw = AS5600_READ_ERROR;
  for (uint8_t attempt = 0U; attempt < 5U; ++attempt) {
    raw = hardware->read_encoder();
    if (raw != AS5600_READ_ERROR) break;
    HAL_Delay(2U);
  }
  hardware->timer->Instance->CCER |= outputs;
  return raw;
}

static uint16_t read_encoder_live(const axis_hardware_t *hardware)
{
  for (uint8_t attempt = 0U; attempt < 3U; ++attempt) {
    uint16_t raw = hardware->read_encoder();
    if (raw != AS5600_READ_ERROR) return raw;
  }
  return AS5600_READ_ERROR;
}

void motor_pwm_init(void)
{
  set_all_compare(&htim1, 0U);
  set_all_compare(&htim8, 0U);
  axis_home_raw[GIMBAL_AXIS_X] = axis_current_raw(GIMBAL_AXIS_X);
  if (axis_home_raw[GIMBAL_AXIS_X] == AS5600_READ_ERROR) {
    axis_home_raw[GIMBAL_AXIS_X] = 0U;
  }
  axis_home_raw[GIMBAL_AXIS_Y] = axis_current_raw(GIMBAL_AXIS_Y);
  if (axis_home_raw[GIMBAL_AXIS_Y] == AS5600_READ_ERROR) {
    axis_home_raw[GIMBAL_AXIS_Y] = 0U;
  }
  axis_home_info[GIMBAL_AXIS_X].homed = false;
  axis_home_info[GIMBAL_AXIS_Y].homed = false;
  axis_home_error[GIMBAL_AXIS_X] = HOME_ERR_NONE;
  axis_home_error[GIMBAL_AXIS_Y] = HOME_ERR_NONE;
  manual_low_valid[GIMBAL_AXIS_X] = false;
  manual_low_valid[GIMBAL_AXIS_Y] = false;
  manual_high_valid[GIMBAL_AXIS_X] = false;
  manual_high_valid[GIMBAL_AXIS_Y] = false;
  hold_enabled = false;
  hold_target_deg[GIMBAL_AXIS_X] = 0U;
  hold_target_deg[GIMBAL_AXIS_Y] = 0U;
  hold_target_raw_command[GIMBAL_AXIS_X] = axis_home_raw[GIMBAL_AXIS_X];
  hold_target_raw_command[GIMBAL_AXIS_Y] = axis_home_raw[GIMBAL_AXIS_Y];
  hold_target_raw_filtered[GIMBAL_AXIS_X] =
      (float)axis_home_raw[GIMBAL_AXIS_X];
  hold_target_raw_filtered[GIMBAL_AXIS_Y] =
      (float)axis_home_raw[GIMBAL_AXIS_Y];
  hold_commutation_ready[GIMBAL_AXIS_X] = false;
  hold_commutation_ready[GIMBAL_AXIS_Y] = false;
  hold_lost_count[GIMBAL_AXIS_X] = 0U;
  hold_lost_count[GIMBAL_AXIS_Y] = 0U;
  hold_error_raw[GIMBAL_AXIS_X] = 0U;
  hold_error_raw[GIMBAL_AXIS_Y] = 0U;
  hold_previous_raw[GIMBAL_AXIS_X] = axis_home_raw[GIMBAL_AXIS_X];
  hold_previous_raw[GIMBAL_AXIS_Y] = axis_home_raw[GIMBAL_AXIS_Y];
  hold_torque_sign[GIMBAL_AXIS_X] = 0;
  hold_torque_sign[GIMBAL_AXIS_Y] = 0;
  hold_torque_polarity[GIMBAL_AXIS_X] =
      GIMBAL_X_FOC_TORQUE_POLARITY >= 0 ? 1 : -1;
  hold_torque_polarity[GIMBAL_AXIS_Y] =
      GIMBAL_Y_FOC_TORQUE_POLARITY >= 0 ? 1 : -1;
  hold_field_step[GIMBAL_AXIS_X] = 0U;
  hold_field_step[GIMBAL_AXIS_Y] = 0U;
  hold_startup_cycles = 0U;
  hold_duty_ticks[GIMBAL_AXIS_X] = 0U;
  hold_duty_ticks[GIMBAL_AXIS_Y] = 0U;
  hold_cycle_count = 0U;
  hold_watchdog_trips = 0U;
  all_motors_enabled = false;
  initialize_continuous_axis(GIMBAL_AXIS_X);
  initialize_continuous_axis(GIMBAL_AXIS_Y);
}

bool motor_pwm_apply_electrical_calibration(
    gimbal_axis_t axis, const GimbalAxisElectricalCalibration *calibration)
{
  if (axis > GIMBAL_AXIS_Y || calibration == NULL ||
      (calibration->raw_direction != 1 && calibration->raw_direction != -1) ||
      (calibration->torque_polarity != 1 &&
       calibration->torque_polarity != -1) ||
      calibration->pole_pairs == 0U || calibration->pole_pairs > 32U ||
      calibration->electrical_offset_raw >= 4096U) {
    return false;
  }
  hold_forward_raw_sign[axis] = calibration->raw_direction;
  hold_torque_polarity[axis] = calibration->torque_polarity;
  hold_pole_pairs[axis] = calibration->pole_pairs;
  hold_electric_offset[axis] = foc_wrap_angle(
      (float)calibration->electrical_offset_raw *
      (FOC_TWO_PI / 4096.0f));
  hold_commutation[axis] = (uint8_t)(
      ((uint32_t)calibration->electrical_offset_raw * SINE_STEPS + 2048U) /
      4096U) % SINE_STEPS;
  hold_commutation_ready[axis] = true;
  return true;
}

bool motor_pwm_get_electrical_calibration(
    gimbal_axis_t axis, GimbalAxisElectricalCalibration *calibration)
{
  if (axis > GIMBAL_AXIS_Y || calibration == NULL ||
      !hold_commutation_ready[axis]) {
    return false;
  }
  calibration->raw_direction = hold_forward_raw_sign[axis];
  calibration->torque_polarity = hold_torque_polarity[axis];
  calibration->pole_pairs = hold_pole_pairs[axis];
  calibration->electrical_offset_raw = (uint16_t)(
      (uint32_t)lroundf(foc_wrap_angle(hold_electric_offset[axis]) *
                        (4096.0f / FOC_TWO_PI))) & 0x0FFFU;
  return true;
}

bool motor_pwm_prepare_continuous_axes(void)
{
  bool ok = true;
  for (uint8_t i = 0U; i < 2U; ++i) {
    gimbal_axis_t axis = (gimbal_axis_t)i;
    if (!axis_is_continuous(axis)) continue;
    if (!align_hold_electrical_angle(axis)) {
      ok = false;
      continue;
    }
    /* Alignment moves the rotor; define zero only after it has settled. */
    initialize_continuous_axis(axis);
    hold_target_deg[axis] = 0U;
  }
  return ok;
}

bool motor_pwm_set_enabled(bool turn_on)
{
  if (turn_on) {
#if GIMBAL_REQUIRE_ENABLE_INPUT
    if (!board_get_enable_input()) {
      board_set_fault(true);
      return false;
    }
#endif
    set_all_compare(&htim1, 0U);
    set_all_compare(&htim8, 0U);
    if (start_timer(&htim1) != HAL_OK || start_timer(&htim8) != HAL_OK) return false;
    HAL_GPIO_WritePin(GIMBAL_X_STBY_GPIO_Port, GIMBAL_X_STBY_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GIMBAL_Y_STBY_GPIO_Port, GIMBAL_Y_STBY_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GIMBAL_X_EN_GPIO_Port, GIMBAL_X_EN_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GIMBAL_Y_EN_GPIO_Port, GIMBAL_Y_EN_Pin, GPIO_PIN_SET);
  } else {
    set_all_compare(&htim1, 0U);
    set_all_compare(&htim8, 0U);
    HAL_GPIO_WritePin(GIMBAL_X_EN_GPIO_Port, GIMBAL_X_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GIMBAL_Y_EN_GPIO_Port, GIMBAL_Y_EN_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GIMBAL_X_STBY_GPIO_Port, GIMBAL_X_STBY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GIMBAL_Y_STBY_GPIO_Port, GIMBAL_Y_STBY_Pin, GPIO_PIN_RESET);
  }
  all_motors_enabled = turn_on;
  return true;
}

bool motor_pwm_is_enabled(void)
{
  return all_motors_enabled;
}

bool motor_pwm_test(gimbal_axis_t axis)
{
  uint16_t start_raw = motor_pwm_read_raw(axis);
  if (start_raw == AS5600_READ_ERROR) return false;
  uint16_t test_raw = (uint16_t)((start_raw + 64U) & 0x0FFFU);
  uint16_t final_raw;
  if (!motor_pwm_set_position(axis, test_raw, &final_raw)) return false;
  return motor_pwm_set_position(axis, start_raw, &final_raw);
}

bool motor_pwm_set_position(gimbal_axis_t axis, uint16_t target_raw,
                            uint16_t *final_raw)
{
  axis_position_error[axis] = 0U;
  limit_event = GIMBAL_LIMIT_NONE;
  limit_event_raw = AS5600_READ_ERROR;
  axis_hardware_t hardware = get_axis_hardware(axis);
  uint16_t current = read_encoder_quiet(&hardware);
  if (target_raw > 4095U || current == AS5600_READ_ERROR) {
    axis_position_error[axis] = 1U;
    return false;
  }
  int16_t initial_error = angle_error(target_raw, current);
  if (initial_error >= -POSITION_DEADBAND &&
      initial_error <= POSITION_DEADBAND) {
    if (final_raw != NULL) *final_raw = current;
    return true;
  }
  if (!hold_commutation_ready[axis] &&
      !align_hold_electrical_angle(axis)) {
    axis_position_error[axis] = 3U;
    return false;
  }
  if (axis == GIMBAL_AXIS_X) {
    board_set_dac_mv(GIMBAL_X_HOLD_DAC_MV, GIMBAL_IDLE_DAC_MV);
  } else {
    board_set_dac_mv(GIMBAL_IDLE_DAC_MV, GIMBAL_Y_HOLD_DAC_MV);
  }
  board_set_boost(true);
  if (!start_axis(&hardware)) {
    axis_position_error[axis] = 2U;
    board_set_boost(false);
    restore_idle_dac();
    return false;
  }

  /* 自动判断正向相序对应编码器增大还是减小，避免硬编码安装方向。 */
  uint8_t stable_samples = 0U;
  uint8_t stall_samples = 0U;
  uint16_t previous = current;
  uint16_t control_previous = current;
  bool reached = false;

  for (uint16_t iteration = 0U; iteration < 600U; ++iteration) {
    if (emergency_stop_requested()) {
      axis_position_error[axis] = 9U;
      break;
    }
    /*
     * Never blank PWM inside the position loop.  The former quiet read
     * disabled all phases for 2 ms every 5 ms and created severe torque
     * ripple, audible chatter and low-speed stalls.
     */
    current = read_encoder_live(&hardware);
    if (current == AS5600_READ_ERROR ||
        !position_guard_allows(axis, current) ||
        HAL_GPIO_ReadPin(hardware.en_port, hardware.en_pin) == GPIO_PIN_RESET ||
        current_limit_fault()) {
      axis_position_error[axis] = 10U;
      break;
    }

    int16_t error = angle_error(target_raw, current);
    if (error >= -POSITION_DEADBAND && error <= POSITION_DEADBAND) {
      foc_apply_position_pd(axis, &hardware, current, target_raw, current);
      if (++stable_samples >= 3U) {
        reached = true;
        break;
      }
      HAL_Delay(10U);
      continue;
    }

    if (axis == GIMBAL_AXIS_X) {
      uint16_t moved = (uint16_t)(angle_error(current, previous));
      if ((int16_t)moved < 0) moved = (uint16_t)(-(int16_t)moved);
      if (moved <= GIMBAL_X_STALL_RAW) {
        if (++stall_samples >= GIMBAL_X_STALL_COUNT) {
          latch_x_limit_event(current);
          axis_position_error[axis] =
              limit_event == GIMBAL_LIMIT_X_MIN ? 12U :
              limit_event == GIMBAL_LIMIT_X_MAX ? 13U : 14U;
          break;
        }
      } else {
        stall_samples = 0U;
      }
      previous = current;
    }

    stable_samples = 0U;
    foc_apply_position_pd(axis, &hardware, current, target_raw,
                          control_previous);
    control_previous = current;
    HAL_Delay(GIMBAL_FOC_PERIOD_MS);
  }

  uint16_t final = read_encoder_quiet(&hardware);
  if (final_raw != NULL) *final_raw = final;
  if (final == AS5600_READ_ERROR) {
    reached = false;
    if (axis_position_error[axis] == 0U) axis_position_error[axis] = 1U;
  }
  stop_axis(&hardware);
  board_set_boost(false);
  restore_idle_dac();
  active_duty_ticks = TEST_DUTY_TICKS;
  if (!reached && axis_position_error[axis] == 0U) {
    axis_position_error[axis] = 11U;
  }
  return reached;
}

uint16_t motor_pwm_get_position_error(gimbal_axis_t axis)
{
  return axis_position_error[axis];
}

gimbal_limit_event_t motor_pwm_get_limit_event(void)
{
  return limit_event;
}

uint16_t motor_pwm_get_limit_event_raw(void)
{
  return limit_event_raw;
}

uint16_t motor_pwm_read_raw(gimbal_axis_t axis)
{
  return axis_current_raw(axis);
}

uint16_t motor_pwm_get_limit_deg(gimbal_axis_t axis)
{
  return axis_limit_deg(axis);
}

void motor_pwm_set_home(gimbal_axis_t axis, uint16_t raw)
{
  if (raw <= 4095U) {
    axis_home_raw[axis] = raw;
    if (axis_home_info[axis].homed) {
      uint16_t travel_counts = (uint16_t)(
          ((uint32_t)axis_home_info[axis].travel_deg * 4096U + 180U) / 360U);
      axis_home_info[axis].low_raw = raw;
      axis_home_info[axis].high_raw = axis_positive_raw_sign(axis) > 0 ?
          (uint16_t)((raw + travel_counts) & 0x0FFFU) :
          (uint16_t)((raw + 4096U - travel_counts) & 0x0FFFU);
      uint16_t half_counts = (uint16_t)(travel_counts / 2U);
      axis_home_info[axis].center_raw = axis_positive_raw_sign(axis) > 0 ?
          (uint16_t)((raw + half_counts) & 0x0FFFU) :
          (uint16_t)((raw + 4096U - half_counts) & 0x0FFFU);
      axis_home_info[axis].center_deg =
          (uint16_t)(axis_home_info[axis].travel_deg / 2U);
    }
  }
}

uint16_t motor_pwm_get_home(gimbal_axis_t axis)
{
  return axis_home_raw[axis];
}

bool motor_pwm_home_from_encoder(gimbal_axis_t axis, uint16_t *home_raw)
{
  uint16_t raw = axis_current_raw(axis);
  if (raw == AS5600_READ_ERROR) return false;
  motor_pwm_set_home(axis, raw);
  if (home_raw != NULL) *home_raw = raw;
  return true;
}

bool motor_pwm_raw_to_deg(gimbal_axis_t axis, uint16_t raw, uint16_t *deg)
{
  if (!motor_pwm_calibration_is_valid(axis)) return false;
  return raw_to_limited_deg(axis, raw, deg);
}

bool motor_pwm_deg_to_raw(gimbal_axis_t axis, uint16_t deg, uint16_t *raw)
{
  if (raw == NULL || !motor_pwm_calibration_is_valid(axis) ||
      deg > axis_home_info[axis].travel_deg) return false;

  uint16_t travel_counts = axis_positive_raw_sign(axis) > 0 ?
      (uint16_t)((axis_home_info[axis].high_raw + 4096U -
                  axis_home_info[axis].low_raw) & 0x0FFFU) :
      (uint16_t)((axis_home_info[axis].low_raw + 4096U -
                  axis_home_info[axis].high_raw) & 0x0FFFU);
  uint32_t delta =
      ((uint32_t)deg * travel_counts + axis_home_info[axis].travel_deg / 2U) /
      axis_home_info[axis].travel_deg;
  *raw = axis_positive_raw_sign(axis) > 0 ?
         (uint16_t)((axis_home_raw[axis] + delta) & 0x0FFFU) :
         (uint16_t)((axis_home_raw[axis] + 4096U - delta) & 0x0FFFU);
  return true;
}

bool motor_pwm_set_position_deg(gimbal_axis_t axis, uint16_t target_deg,
                                uint16_t *final_deg, uint16_t *final_raw)
{
  if (final_deg != NULL) *final_deg = UINT16_MAX;
  if (final_raw != NULL) *final_raw = AS5600_READ_ERROR;
  if (!motor_pwm_calibration_is_valid(axis) ||
      target_deg > axis_home_info[axis].travel_deg) {
    axis_position_error[axis] = 15U;
    return false;
  }

  uint16_t current_raw = axis_current_raw(axis);
  uint16_t current_deg;
  if (!motor_pwm_raw_to_deg(axis, current_raw, &current_deg)) {
    axis_position_error[axis] = 1U;
    return false;
  }

  if (!axis_is_continuous(axis)) {
    position_guard_min_deg = current_deg < target_deg ? current_deg : target_deg;
    position_guard_max_deg = current_deg > target_deg ? current_deg : target_deg;
    position_guard_min_deg = position_guard_min_deg > POSITION_GUARD_MARGIN_DEG ?
                             position_guard_min_deg - POSITION_GUARD_MARGIN_DEG :
                             0U;
    position_guard_max_deg += POSITION_GUARD_MARGIN_DEG;
    if (position_guard_max_deg > axis_limit_deg(axis)) {
      position_guard_max_deg = axis_limit_deg(axis);
    }
    position_guard_enabled = true;
  }

  uint16_t target_raw;
  uint16_t reached_raw = AS5600_READ_ERROR;
  bool reached = motor_pwm_deg_to_raw(axis, target_deg, &target_raw) &&
                 motor_pwm_set_position(axis, target_raw, &reached_raw);
  position_guard_enabled = false;
  if (final_raw != NULL) *final_raw = reached_raw;
  if (final_deg != NULL) {
    uint16_t deg;
    *final_deg = motor_pwm_raw_to_deg(axis, reached_raw, &deg) ?
                 deg : UINT16_MAX;
  }
  if (reached &&
      (reached_raw == AS5600_READ_ERROR ||
       (final_deg != NULL && *final_deg == UINT16_MAX))) {
    axis_position_error[axis] = 1U;
    reached = false;
  }
  return reached;
}

void motor_pwm_select_position_power(gimbal_axis_t axis,
                                     int8_t mechanical_dir)
{
  active_duty_ticks = axis_center_duty(axis, mechanical_dir);
  set_axis_homing_dac_mv(axis, axis_center_dac(axis, mechanical_dir));
}

void motor_pwm_restore_idle_power(void)
{
  active_duty_ticks = TEST_DUTY_TICKS;
  restore_idle_dac();
}

bool motor_pwm_auto_home_axis(gimbal_axis_t axis, gimbal_axis_home_t *home)
{
  axis_hardware_t hardware = get_axis_hardware(axis);
  uint32_t saved_duty = active_duty_ticks;
  gimbal_axis_home_t result = {0};

  axis_home_error[axis] = HOME_ERR_NONE;
  axis_home_info[axis].homed = false;
  if (!hold_commutation_ready[axis] &&
      !align_hold_electrical_angle(axis)) {
    active_duty_ticks = saved_duty;
    axis_home_error[axis] = HOME_ERR_CAL;
    return false;
  }
  uint16_t homing_dac_mv = axis == GIMBAL_AXIS_X ? GIMBAL_X_HOMING_DAC_MV :
                           GIMBAL_Y_HOMING_DAC_MV;
  set_axis_homing_dac_mv(axis, homing_dac_mv);
  board_set_boost(true);
  active_duty_ticks = axis_homing_duty(axis);
  if (!start_axis(&hardware)) {
    active_duty_ticks = saved_duty;
    board_set_boost(false);
    restore_idle_dac();
    axis_home_error[axis] = HOME_ERR_START;
    return false;
  }

  bool ok = true;

  uint16_t low_raw = AS5600_READ_ERROR;
  uint16_t high_raw = AS5600_READ_ERROR;
  uint16_t center_raw = AS5600_READ_ERROR;
  uint16_t travel_counts = 0U;
  uint16_t first_travel_counts = 0U;
  int8_t center_move_dir = -1;
  if (ok) {
    ok = scan_to_mechanical_stop(axis, &hardware, -1, true,
                                 &low_raw, &first_travel_counts,
                                 &homing_dac_mv);
    if (!ok && first_travel_counts < first_stop_min_counts()) {
      /*
       * Starting close to the negative stop is valid. Probe the positive
       * stop first, then traverse the complete range back to the negative
       * stop. This removes the old requirement that boot must start at least
       * 60 degrees away from the first endpoint.
       */
      uint16_t first_high_travel = 0U;
      ok = scan_to_mechanical_stop(axis, &hardware, 1, true,
                                   &high_raw, &first_high_travel,
                                   &homing_dac_mv);
      if (ok) {
        HAL_Delay(120U);
        ok = scan_to_mechanical_stop(axis, &hardware, -1, false,
                                     &low_raw, &travel_counts,
                                     &homing_dac_mv);
        center_move_dir = 1;
      }
    }
    if (!ok) axis_home_error[axis] = HOME_ERR_LOW_STOP;
  }
  if (ok && center_move_dir < 0) {
    HAL_Delay(120U);
    ok = scan_to_mechanical_stop(axis, &hardware, 1, false,
                                 &high_raw, &travel_counts,
                                 &homing_dac_mv);
    if (!ok) axis_home_error[axis] = HOME_ERR_HIGH_STOP;
  }

  if (ok && low_raw != AS5600_READ_ERROR && high_raw != AS5600_READ_ERROR) {
    travel_counts = axis_positive_raw_sign(axis) > 0 ?
                    (uint16_t)((high_raw + 4096U - low_raw) & 0x0FFFU) :
                    (uint16_t)((low_raw + 4096U - high_raw) & 0x0FFFU);
  }
  if (!ok || low_raw == AS5600_READ_ERROR || high_raw == AS5600_READ_ERROR ||
      travel_counts < 128U) {
    stop_axis(&hardware);
    active_duty_ticks = saved_duty;
    result.homed = false;
    result.low_raw = low_raw == AS5600_READ_ERROR ? 0U : low_raw;
    result.high_raw = high_raw == AS5600_READ_ERROR ? 0U : high_raw;
    result.center_raw = 0U;
    uint16_t reported_counts = axis_home_error[axis] == HOME_ERR_LOW_STOP ?
                               first_travel_counts : travel_counts;
    result.travel_deg = (uint16_t)(((uint32_t)reported_counts * 360U +
                                    2048U) / 4096U);
    result.center_deg = 0U;
    axis_home_info[axis] = result;
    if (axis_home_error[axis] == HOME_ERR_NONE) {
      axis_home_error[axis] = HOME_ERR_TRAVEL;
    }
    board_set_boost(false);
    restore_idle_dac();
    return false;
  }

  /*
   * Use the absolute endpoint separation as the authoritative travel.
   * Accumulated one-way progress can be inflated by endpoint oscillation and
   * previously allowed a short, invalid range to pass calibration.
   */
  travel_counts = axis_positive_raw_sign(axis) > 0 ?
                  (uint16_t)((high_raw + 4096U - low_raw) & 0x0FFFU) :
                  (uint16_t)((low_raw + 4096U - high_raw) & 0x0FFFU);
  if (travel_counts < 128U) {
    stop_axis(&hardware);
    active_duty_ticks = saved_duty;
    axis_home_error[axis] = HOME_ERR_TRAVEL;
    board_set_boost(false);
    restore_idle_dac();
    return false;
  }
  uint16_t measured_deg = axis_limit_deg(axis);

  uint16_t center_deg = (uint16_t)(measured_deg / 2U);
  axis_home_raw[axis] = low_raw;

  uint16_t half_counts = (uint16_t)(travel_counts / 2U);
  center_raw = axis_positive_raw_sign(axis) > 0 ?
               (uint16_t)((low_raw + half_counts) & 0x0FFFU) :
               (uint16_t)((low_raw + 4096U - half_counts) & 0x0FFFU);
  result.homed = false;
  result.low_raw = low_raw;
  result.high_raw = high_raw;
  result.center_raw = center_raw;
  result.travel_deg = measured_deg;
  result.center_deg = center_deg;
  axis_home_info[axis] = result;

  ok = move_to_raw_in_direction(axis, &hardware, center_move_dir, center_raw,
                                axis_max_homing_steps(axis), &center_raw);

  stop_axis(&hardware);
  active_duty_ticks = saved_duty;

  if (!ok) {
    axis_home_error[axis] = HOME_ERR_CENTER;
    board_set_boost(false);
    restore_idle_dac();
    return false;
  }

  uint16_t actual_center_deg;
  if (!raw_to_limited_deg(axis, center_raw, &actual_center_deg)) {
    axis_home_error[axis] = HOME_ERR_CENTER;
    board_set_boost(false);
    restore_idle_dac();
    return false;
  }
  uint16_t center_error = actual_center_deg > center_deg ?
                          (uint16_t)(actual_center_deg - center_deg) :
                          (uint16_t)(center_deg - actual_center_deg);
  if (center_error > 8U) {
    axis_home_error[axis] = HOME_ERR_CENTER;
    board_set_boost(false);
    return false;
  }

  result.homed = true;
  result.low_raw = low_raw;
  result.high_raw = high_raw;
  result.center_raw = center_raw;
  result.travel_deg = measured_deg;
  result.center_deg = center_deg;
  axis_home_info[axis] = result;
  hold_target_deg[axis] = result.center_deg;
  if (home != NULL) *home = result;

  board_set_boost(false);
  restore_idle_dac();
  return true;
}

bool motor_pwm_auto_home_all(void)
{
  motor_pwm_hold_enable(false);
  bool x_ok = motor_pwm_auto_home_axis(GIMBAL_AXIS_X, NULL);
  bool y_ok = motor_pwm_auto_home_axis(GIMBAL_AXIS_Y, NULL);
  return x_ok && y_ok;
}

bool motor_pwm_mark_manual_limit(gimbal_axis_t axis, bool low_end,
                                 uint16_t *raw)
{
  uint16_t value = motor_pwm_read_raw(axis);
  if (value == AS5600_READ_ERROR) return false;

  if (!motor_pwm_set_manual_limit_raw(axis, low_end, value)) return false;
  if (raw != NULL) *raw = value;
  return true;
}

bool motor_pwm_set_manual_limit_raw(gimbal_axis_t axis, bool low_end,
                                    uint16_t value)
{
  if (value > 4095U) return false;

  gimbal_axis_home_t info = axis_home_info[axis];
  if (low_end) {
    info.low_raw = value;
    manual_low_valid[axis] = true;
    manual_high_valid[axis] = false;
    axis_home_raw[axis] = value;
  } else {
    info.high_raw = value;
    manual_high_valid[axis] = true;
  }

  info.homed = false;
  info.center_raw = 0U;
  info.center_deg = 0U;
  info.travel_deg = 0U;

  if (manual_low_valid[axis] && manual_high_valid[axis]) {
    uint16_t travel_counts = axis_positive_raw_sign(axis) > 0 ?
                             (uint16_t)((info.high_raw + 4096U -
                                         info.low_raw) & 0x0FFFU) :
                             (uint16_t)((info.low_raw + 4096U -
                                         info.high_raw) & 0x0FFFU);
    uint16_t travel_deg =
        travel_counts >= 128U ? axis_limit_deg(axis) : 0U;

    uint16_t half_counts = (uint16_t)(travel_counts / 2U);
    info.center_raw = axis_positive_raw_sign(axis) > 0 ?
                      (uint16_t)((info.low_raw + half_counts) & 0x0FFFU) :
                      (uint16_t)((info.low_raw + 4096U - half_counts) &
                                 0x0FFFU);
    info.travel_deg = travel_deg;
    info.center_deg = (uint16_t)(travel_deg / 2U);
    info.homed = travel_deg >= axis_min_calibration_deg(axis);
    if (info.homed) {
      axis_home_raw[axis] = info.low_raw;
      axis_home_error[axis] = HOME_ERR_NONE;
      hold_target_deg[axis] = info.center_deg;
    } else {
      axis_home_error[axis] = HOME_ERR_TRAVEL;
    }
  }

  axis_home_info[axis] = info;
  return true;
}

bool motor_pwm_move_to_marked_center(gimbal_axis_t axis, uint16_t *final_raw)
{
  if (!motor_pwm_calibration_is_valid(axis)) return false;
  return motor_pwm_set_position(axis, axis_home_info[axis].center_raw,
                                final_raw);
}

uint16_t motor_pwm_get_home_error(gimbal_axis_t axis)
{
  return axis_home_error[axis];
}

bool motor_pwm_is_homed(gimbal_axis_t axis)
{
  return axis_home_info[axis].homed;
}

bool motor_pwm_calibration_is_valid(gimbal_axis_t axis)
{
  const gimbal_axis_home_t *info = &axis_home_info[axis];
  if (!info->homed || info->low_raw > 4095U || info->high_raw > 4095U ||
      info->center_raw > 4095U ||
      info->travel_deg < axis_min_calibration_deg(axis) ||
      info->travel_deg > axis_limit_deg(axis)) {
    return false;
  }
  return true;
}

bool motor_pwm_get_home_info(gimbal_axis_t axis, gimbal_axis_home_t *home)
{
  if (home == NULL) return false;
  *home = axis_home_info[axis];
  return axis_home_info[axis].homed;
}

void motor_pwm_clear_calibration(void)
{
  hold_enabled = false;
  for (uint8_t i = 0U; i < 2U; ++i) {
    axis_home_info[i] = (gimbal_axis_home_t){0};
    axis_home_error[i] = HOME_ERR_NONE;
    manual_low_valid[i] = false;
    manual_high_valid[i] = false;
    hold_target_deg[i] = 0U;
    hold_commutation_ready[i] = false;
    hold_lost_count[i] = 0U;
    hold_encoder_error_count[i] = 0U;
    hold_torque_sign[i] = 0;
  }
  initialize_continuous_axis(GIMBAL_AXIS_X);
  initialize_continuous_axis(GIMBAL_AXIS_Y);
}

static bool align_hold_electrical_angle(gimbal_axis_t axis)
{
  axis_hardware_t hardware = get_axis_hardware(axis);
  active_duty_ticks = axis_hold_duty(axis);
  if (axis == GIMBAL_AXIS_X) {
    board_set_dac_mv(GIMBAL_X_HOLD_DAC_MV, GIMBAL_IDLE_DAC_MV);
  } else {
    board_set_dac_mv(GIMBAL_IDLE_DAC_MV, GIMBAL_Y_HOLD_DAC_MV);
  }
  board_set_boost(true);
  if (!start_axis(&hardware)) return false;

  /*
   * Vendor-reference alignment: lock the rotor, then sweep a stator vector
   * slowly through 3*pi/2.  Encoder displacement determines electrical
   * direction; the final vector determines zero electrical angle.
   */
  update_foc_vector(hardware.timer, 0.0f, active_duty_ticks);
  HAL_Delay(500U);
  uint16_t raw_start = read_encoder_quiet(&hardware);
  if (raw_start == AS5600_READ_ERROR) {
    stop_axis(&hardware);
    board_set_boost(false);
    restore_idle_dac();
    return false;
  }

  const uint16_t sweep_steps = 500U;
  const float final_field = 4.71238898038f;
  for (uint16_t i = 1U; i <= sweep_steps; ++i) {
    float field = final_field * (float)i / (float)sweep_steps;
    update_foc_vector(hardware.timer, field, active_duty_ticks);
    HAL_Delay(2U);
  }
  HAL_Delay(500U);
  uint16_t aligned_raw = read_encoder_quiet(&hardware);
  if (aligned_raw == AS5600_READ_ERROR) {
    stop_axis(&hardware);
    board_set_boost(false);
    restore_idle_dac();
    active_duty_ticks = TEST_DUTY_TICKS;
    return false;
  }

  int16_t moved = angle_error(aligned_raw, raw_start);
  if (moved >= -4 && moved <= 4) {
    stop_axis(&hardware);
    board_set_boost(false);
    restore_idle_dac();
    active_duty_ticks = TEST_DUTY_TICKS;
    return false;
  }
  hold_forward_raw_sign[axis] = moved > 0 ? 1 : -1;

  /*
   * The alignment field swept through 3*pi/2 (3072 encoder counts for one
   * pole pair). Rotor travel therefore identifies the real pole-pair count.
   * A wrong fixed value aligns correctly only at the boot position, then its
   * phase error grows with mechanical angle and produces angle-specific
   * violent oscillation.
   */
#if GIMBAL_X_AUTO_DETECT_POLE_PAIRS
  if (axis == GIMBAL_AXIS_X) {
    uint16_t moved_abs = moved < 0 ? (uint16_t)-moved : (uint16_t)moved;
    uint16_t detected =
        (uint16_t)((3072U + moved_abs / 2U) / moved_abs);
    if (detected >= 2U && detected <= 32U) {
      hold_pole_pairs[axis] = (uint8_t)detected;
    }
  }
#endif

  float shaft = (float)aligned_raw * (FOC_TWO_PI / 4096.0f);
  hold_electric_offset[axis] =
      foc_wrap_angle(final_field -
                     (float)hold_forward_raw_sign[axis] *
                     (float)hold_pole_pairs[axis] * shaft);
  uint32_t electric_step =
      (uint32_t)(hold_electric_offset[axis] *
                 ((float)SINE_STEPS / FOC_TWO_PI) + 0.5f) % SINE_STEPS;
  hold_commutation[axis] =
      (uint8_t)electric_step;

  /*
   * Probe a positive encoder torque after alignment.  Phase order and motor
   * wiring can make a compile-time polarity unreliable; determine it on the
   * actual bridge every time electrical alignment is performed.
   */
  hold_torque_polarity[axis] = 1;
  uint32_t probe_amplitude = axis_hold_min_duty(axis) +
      (axis == GIMBAL_AXIS_X ? GIMBAL_X_POSITION_STATIC_DUTY :
                               GIMBAL_Y_POSITION_STATIC_DUTY);
  if (probe_amplitude > axis_hold_duty(axis)) {
    probe_amplitude = axis_hold_duty(axis);
  }
  float probe_field = axis_electrical_angle(axis, aligned_raw);
  if (hold_forward_raw_sign[axis] > 0) {
    probe_field += FOC_HALF_PI;
  } else {
    probe_field -= FOC_HALF_PI;
  }
  /* Re-lock first so residual motion from the alignment sweep is excluded. */
  update_foc_vector(hardware.timer,
                    axis_electrical_angle(axis, aligned_raw),
                    axis_hold_min_duty(axis));
  HAL_Delay(300U);
  aligned_raw = read_encoder_quiet(&hardware);
  if (aligned_raw == AS5600_READ_ERROR) {
    stop_axis(&hardware);
    board_set_boost(false);
    restore_idle_dac();
    active_duty_ticks = TEST_DUTY_TICKS;
    return false;
  }
  probe_field = axis_electrical_angle(axis, aligned_raw);
  probe_field += hold_forward_raw_sign[axis] > 0 ?
                 FOC_HALF_PI : -FOC_HALF_PI;
  update_foc_vector(hardware.timer, probe_field, probe_amplitude);
  HAL_Delay(300U);
  /* Sample while probe torque is still active; coast-back can invert delta. */
  uint16_t probe_raw = read_encoder_quiet(&hardware);
  if (probe_raw != AS5600_READ_ERROR) {
    int16_t probe_delta = angle_error(probe_raw, aligned_raw);
    if (probe_delta < 0) hold_torque_polarity[axis] = -1;
  }

  stop_axis(&hardware);
  board_set_boost(false);
  restore_idle_dac();
  active_duty_ticks = TEST_DUTY_TICKS;
  hold_commutation_ready[axis] = true;
  return true;
}

void motor_pwm_hold_enable(bool enabled)
{
  if (!enabled) {
    hold_enabled = false;
    axis_hardware_t x_hardware = get_axis_hardware(GIMBAL_AXIS_X);
    axis_hardware_t y_hardware = get_axis_hardware(GIMBAL_AXIS_Y);
    stop_axis(&x_hardware);
    stop_axis(&y_hardware);
    all_motors_enabled = false;
    hold_watchdog_deadline = 0U;
    hold_duty_ticks[GIMBAL_AXIS_X] = 0U;
    hold_duty_ticks[GIMBAL_AXIS_Y] = 0U;
    hold_integral_effort[GIMBAL_AXIS_X] = 0.0f;
    hold_integral_effort[GIMBAL_AXIS_Y] = 0.0f;
    hold_integral_qualify_count[GIMBAL_AXIS_X] = 0U;
    hold_integral_qualify_count[GIMBAL_AXIS_Y] = 0U;
    hold_settle_count[GIMBAL_AXIS_X] = 0U;
    hold_settle_count[GIMBAL_AXIS_Y] = 0U;
    hold_settled[GIMBAL_AXIS_X] = false;
    hold_settled[GIMBAL_AXIS_Y] = false;
    hold_torque_sign[GIMBAL_AXIS_X] = 0;
    hold_torque_sign[GIMBAL_AXIS_Y] = 0;
    hold_reverse_neutral_count[GIMBAL_AXIS_X] = 0U;
    hold_reverse_neutral_count[GIMBAL_AXIS_Y] = 0U;
    board_set_boost(false);
    restore_idle_dac();
    return;
  }

#if GIMBAL_REQUIRE_ENABLE_INPUT
  if (!board_get_enable_input()) {
    board_set_fault(true);
    return;
  }
#endif

  hold_enabled = motor_pwm_calibration_is_valid(GIMBAL_AXIS_X) &&
                 motor_pwm_calibration_is_valid(GIMBAL_AXIS_Y);
  for (uint8_t i = 0U; hold_enabled && i < 2U; ++i) {
    if (!hold_commutation_ready[i]) {
      hold_enabled = align_hold_electrical_angle((gimbal_axis_t)i);
    }
    hold_lost_count[i] = 0U;
    hold_torque_sign[i] = 0;
    hold_reverse_neutral_count[i] = 0U;
  }
  for (uint8_t i = 0U; hold_enabled && i < 2U; ++i) {
    uint16_t raw = motor_pwm_read_raw((gimbal_axis_t)i);
    uint16_t deg;
    if (!motor_pwm_raw_to_deg((gimbal_axis_t)i, raw, &deg)) {
      hold_enabled = false;
    } else {
      hold_target_deg[i] = deg;
      hold_target_raw_command[i] = raw;
      hold_target_raw_filtered[i] = (float)raw;
      hold_previous_raw[i] = raw;
      hold_last_encoder_raw[i] = raw;
      hold_control_raw_filtered[i] = (float)raw;
      hold_speed_filtered[i] = 0.0f;
      hold_integral_effort[i] = 0.0f;
      hold_integral_qualify_count[i] = 0U;
      hold_settle_count[i] = 0U;
      hold_settled[i] = false;
    }
  }
  if (hold_enabled) {
    axis_hardware_t x_hardware = get_axis_hardware(GIMBAL_AXIS_X);
    axis_hardware_t y_hardware = get_axis_hardware(GIMBAL_AXIS_Y);
    board_set_dac_mv(GIMBAL_X_HOLD_DAC_MV, GIMBAL_Y_HOLD_DAC_MV);
    board_set_boost(true);
    hold_enabled = start_axis(&x_hardware) && start_axis(&y_hardware);
    all_motors_enabled = hold_enabled;
    if (hold_enabled) board_set_fault(false);
  }
  if (!hold_enabled) {
    motor_pwm_hold_enable(false);
    return;
  }
  next_hold_tick = HAL_GetTick();
  hold_watchdog_deadline = next_hold_tick + GIMBAL_FOC_WATCHDOG_MS;
  hold_cycle_count = 0U;
  hold_startup_cycles = 0U;
}

bool motor_pwm_hold_is_enabled(void)
{
  return hold_enabled;
}

bool motor_pwm_hold_set_target(gimbal_axis_t axis, uint16_t target_deg)
{
  if (!motor_pwm_calibration_is_valid(axis) ||
      target_deg > axis_home_info[axis].travel_deg) {
    return false;
  }
  hold_target_deg[axis] = target_deg;
  uint16_t target_raw;
  if (!motor_pwm_deg_to_raw(axis, target_deg, &target_raw)) return false;
  int16_t command_delta =
      angle_error(target_raw, hold_target_raw_command[axis]);
  if (axis == GIMBAL_AXIS_X && command_delta != 0) {
    /*
     * Preserve the current load estimate across target updates.  Pitch can
     * rotate through a full turn, so gravity may resist either raw direction;
     * seeding one fixed direction only works on one side of the mechanism.
     */
    hold_integral_qualify_count[axis] = 0U;
    hold_settle_count[axis] = 0U;
    hold_settled[axis] = false;
  }
  hold_target_raw_command[axis] = target_raw;
  return true;
}

bool motor_pwm_hold_set_target_tenths(gimbal_axis_t axis,
                                      uint16_t target_tenths)
{
  if (!motor_pwm_calibration_is_valid(axis) ||
      target_tenths > (uint16_t)(axis_home_info[axis].travel_deg * 10U)) {
    return false;
  }
  uint16_t travel_counts = axis_positive_raw_sign(axis) > 0 ?
      (uint16_t)((axis_home_info[axis].high_raw + 4096U -
                  axis_home_info[axis].low_raw) & 0x0FFFU) :
      (uint16_t)((axis_home_info[axis].low_raw + 4096U -
                  axis_home_info[axis].high_raw) & 0x0FFFU);
  uint32_t travel_tenths =
      (uint32_t)axis_home_info[axis].travel_deg * 10U;
  if (travel_counts == 0U || travel_tenths == 0U) return false;
  uint16_t offset = (uint16_t)(
      ((uint32_t)target_tenths * travel_counts + travel_tenths / 2U) /
      travel_tenths);
  uint16_t target_raw = axis_positive_raw_sign(axis) > 0 ?
      (uint16_t)((axis_home_info[axis].low_raw + offset) & 0x0FFFU) :
      (uint16_t)((axis_home_info[axis].low_raw + 4096U - offset) & 0x0FFFU);
  int16_t command_delta =
      angle_error(target_raw, hold_target_raw_command[axis]);
  if (axis == GIMBAL_AXIS_X && command_delta != 0) {
    hold_integral_qualify_count[axis] = 0U;
    hold_settle_count[axis] = 0U;
    hold_settled[axis] = false;
  }
  hold_target_raw_command[axis] = target_raw;
  hold_target_deg[axis] = (uint16_t)((target_tenths + 5U) / 10U);
  return true;
}

uint16_t motor_pwm_hold_get_target(gimbal_axis_t axis)
{
  return hold_target_deg[axis];
}

void motor_pwm_hold_process(void)
{
  if (!hold_enabled) return;

#if GIMBAL_REQUIRE_ENABLE_INPUT
  if (!board_get_enable_input()) {
    motor_pwm_hold_enable(false);
    board_set_fault(true);
    return;
  }
#endif

  uint32_t now = HAL_GetTick();
  if ((int32_t)(now - next_hold_tick) < 0) return;
  next_hold_tick = now + GIMBAL_FOC_PERIOD_MS;
  hold_watchdog_deadline = now + GIMBAL_FOC_WATCHDOG_MS;
  ++hold_cycle_count;

  for (uint8_t i = 0U; i < 2U; ++i) {
    gimbal_axis_t axis = (gimbal_axis_t)i;
    axis_hardware_t hardware = get_axis_hardware(axis);
    uint16_t raw = motor_pwm_read_raw(axis);
    uint16_t deg;
    if (raw == AS5600_READ_ERROR) {
      if (++hold_encoder_error_count[axis] >=
          GIMBAL_FOC_ENCODER_ERROR_COUNT) {
        axis_position_error[axis] = 15U;
        motor_pwm_hold_enable(false);
        board_set_fault(true);
        return;
      }
      /* A PWM noise spike must not turn one missed sample into a stop. */
      raw = hold_last_encoder_raw[axis];
    } else {
      hold_encoder_error_count[axis] = 0U;
      hold_last_encoder_raw[axis] = raw;
    }
    if (!motor_pwm_raw_to_deg(axis, raw, &deg)) {
      continue;
    }

    uint16_t filtered_raw =
        (uint16_t)lroundf(hold_target_raw_filtered[axis]) & 0x0FFFU;
    int16_t target_delta =
        angle_error(hold_target_raw_command[axis], filtered_raw);
    float step;
    if (axis == GIMBAL_AXIS_X && target_delta < 0) {
      /* Negative raw direction is physical pitch-down on this assembly. */
      step = GIMBAL_X_TRACK_DOWN_TARGET_SLEW_RAW;
    } else {
      step = axis == GIMBAL_AXIS_X ?
          GIMBAL_X_TRACK_TARGET_SLEW_RAW :
          GIMBAL_Y_TRACK_TARGET_SLEW_RAW;
    }
    if ((float)target_delta > step) {
      hold_target_raw_filtered[axis] += step;
    } else if ((float)target_delta < -step) {
      hold_target_raw_filtered[axis] -= step;
    } else {
      hold_target_raw_filtered[axis] =
          (float)hold_target_raw_command[axis];
    }
    while (hold_target_raw_filtered[axis] >= 4096.0f) {
      hold_target_raw_filtered[axis] -= 4096.0f;
    }
    while (hold_target_raw_filtered[axis] < 0.0f) {
      hold_target_raw_filtered[axis] += 4096.0f;
    }
    uint16_t target_raw =
        (uint16_t)lroundf(hold_target_raw_filtered[axis]) & 0x0FFFU;

    uint16_t filtered_measurement =
        (uint16_t)lroundf(hold_control_raw_filtered[axis]) & 0x0FFFU;
    int16_t measurement_delta = angle_error(raw, filtered_measurement);
    float measurement_alpha = axis == GIMBAL_AXIS_X ?
        GIMBAL_X_TRACK_POSITION_FILTER_ALPHA :
        GIMBAL_Y_TRACK_POSITION_FILTER_ALPHA;
    hold_control_raw_filtered[axis] +=
        measurement_alpha * (float)measurement_delta;
    while (hold_control_raw_filtered[axis] >= 4096.0f) {
      hold_control_raw_filtered[axis] -= 4096.0f;
    }
    while (hold_control_raw_filtered[axis] < 0.0f) {
      hold_control_raw_filtered[axis] += 4096.0f;
    }
    uint16_t control_raw =
        (uint16_t)lroundf(hold_control_raw_filtered[axis]) & 0x0FFFU;

    int16_t raw_error = angle_error(target_raw, control_raw);
    uint16_t error_abs = raw_error < 0 ? (uint16_t)-raw_error :
                                          (uint16_t)raw_error;
    hold_error_raw[axis] = error_abs;
    int32_t speed_sample =
        angle_error(control_raw, hold_previous_raw[axis]);
    hold_speed_filtered[axis] += GIMBAL_TRACK_SPEED_FILTER_ALPHA *
        ((float)speed_sample - hold_speed_filtered[axis]);
    int32_t speed = (int32_t)lroundf(hold_speed_filtered[axis]);
    int32_t kp = axis == GIMBAL_AXIS_X ?
                 GIMBAL_X_TRACK_KP : GIMBAL_Y_TRACK_KP;
    int32_t kd = axis == GIMBAL_AXIS_X ?
                 GIMBAL_X_TRACK_KD : GIMBAL_Y_TRACK_KD;
    if (axis == GIMBAL_AXIS_X && raw_error < 0) {
      kd = GIMBAL_X_TRACK_DOWN_KD;
    }
    int32_t control_error = (int32_t)raw_error * kp - speed * kd;
    if (axis == GIMBAL_AXIS_X) {
      int16_t target_delta_abs =
          target_delta < 0 ? (int16_t)-target_delta : target_delta;

      /*
       * While pitch is following an active ramp, a short gravity-assisted
       * speed spike must not make the D term reverse motor torque.  That
       * brake/restart cycle is felt as strong uphill judder.
       */
      if (target_delta > 2 && raw_error > 0 && control_error < 1) {
        control_error = 1;
      } else if (target_delta < -2 && raw_error < 0 &&
                 control_error > -1) {
        control_error = -1;
      }

      if (target_delta_abs > 2 ||
          error_abs > GIMBAL_X_SETTLE_EXIT_ERROR_RAW) {
        hold_settled[axis] = false;
        hold_settle_count[axis] = 0U;
      } else if (!hold_settled[axis] &&
                 error_abs <= GIMBAL_X_SETTLE_ERROR_RAW &&
                 speed <= GIMBAL_X_SETTLE_SPEED_RAW &&
                 speed >= -GIMBAL_X_SETTLE_SPEED_RAW) {
        if (++hold_settle_count[axis] >= GIMBAL_X_SETTLE_CYCLES) {
          hold_settled[axis] = true;
          hold_settle_count[axis] = GIMBAL_X_SETTLE_CYCLES;
        }
      } else if (!hold_settled[axis]) {
        hold_settle_count[axis] = 0U;
      }

      /*
       * Learn gravity torque only outside the settled band.  Integrating
       * one-count encoder bias while stationary slowly creates a self-starting
       * limit cycle: quiet for a few seconds, then a large correction.
       * Inside the band, retain (freeze) the learned holding torque.
       */
      if (!hold_settled[axis] && target_delta_abs > 2) {
        /*
         * Online load observer for a full-rotation pitch axis.  Tracking lag
         * integrates into a signed feed-forward torque, automatically
         * following gravity as its direction changes across the rotation.
         */
        bool motion_stalled =
            (target_delta > 0 && speed <= 0) ||
            (target_delta < 0 && speed >= 0);
        if (motion_stalled &&
            error_abs >= GIMBAL_X_TRACK_INTEGRAL_MIN_ERROR_RAW) {
          hold_integral_effort[axis] +=
              (float)raw_error * GIMBAL_X_TRACK_MOTION_INTEGRAL_GAIN;
        }
        hold_integral_qualify_count[axis] = 0U;
      } else if (!hold_settled[axis] &&
          target_delta_abs <= 2 &&
          error_abs >= GIMBAL_X_TRACK_INTEGRAL_MIN_ERROR_RAW &&
          speed <= GIMBAL_X_TRACK_INTEGRAL_ACTIVE_SPEED_RAW &&
          speed >= -GIMBAL_X_TRACK_INTEGRAL_ACTIVE_SPEED_RAW) {
        if (hold_integral_qualify_count[axis] <
            GIMBAL_X_TRACK_INTEGRAL_QUALIFY_CYCLES) {
          ++hold_integral_qualify_count[axis];
        } else {
          hold_integral_effort[axis] +=
              (float)raw_error * GIMBAL_X_TRACK_INTEGRAL_GAIN;
        }
      } else if (target_delta_abs > 2 ||
                 speed > GIMBAL_X_TRACK_INTEGRAL_ACTIVE_SPEED_RAW ||
                 speed < -GIMBAL_X_TRACK_INTEGRAL_ACTIVE_SPEED_RAW) {
        hold_integral_qualify_count[axis] = 0U;
        /*
         * Never leak pitch support while the target ramp is still moving.
         * Pitch-up support has the same sign as shaft motion, so the previous
         * test decayed it every 5 ms and made a long uphill move start smooth
         * but judder after one or two seconds.  Leakage is only useful once
         * the commanded ramp has reached its endpoint.
         */
        if (target_delta_abs <= 2 &&
            (float)speed * hold_integral_effort[axis] > 0.0f) {
          hold_integral_effort[axis] *= GIMBAL_X_TRACK_INTEGRAL_LEAK;
        }
      } else {
        hold_integral_qualify_count[axis] = 0U;
      }
      if (hold_integral_effort[axis] >
          GIMBAL_X_TRACK_INTEGRAL_LIMIT) {
        hold_integral_effort[axis] = GIMBAL_X_TRACK_INTEGRAL_LIMIT;
      } else if (hold_integral_effort[axis] <
                 -GIMBAL_X_TRACK_INTEGRAL_LIMIT) {
        hold_integral_effort[axis] = -GIMBAL_X_TRACK_INTEGRAL_LIMIT;
      }
      if (hold_settled[axis]) {
        /* Quiet endpoint hold: fixed learned gravity torque, no PD chatter. */
        control_error =
            (int32_t)lroundf(hold_integral_effort[axis]);
      } else {
        control_error +=
            (int32_t)lroundf(hold_integral_effort[axis]);
      }

    }
    hold_previous_raw[axis] = control_raw;
    if (error_abs >= GIMBAL_FOC_LOST_ERROR_RAW) {
      if (++hold_lost_count[axis] >= GIMBAL_FOC_LOST_COUNT) {
        if (axis == GIMBAL_AXIS_X) latch_x_limit_event(raw);
        axis_position_error[axis] = axis == GIMBAL_AXIS_X ? 14U : 11U;
        motor_pwm_hold_enable(false);
        board_set_fault(true);
        return;
      }
    } else {
      hold_lost_count[axis] = 0U;
    }

    int8_t requested_torque_sign = control_error >= 0 ? 1 : -1;
    uint32_t duty_target;
    uint32_t effort_abs = control_error < 0 ?
                          (uint32_t)-control_error :
                          (uint32_t)control_error;
    uint32_t torque_enter_effort = axis == GIMBAL_AXIS_X ?
        GIMBAL_X_TRACK_TORQUE_ENTER_EFFORT :
        GIMBAL_Y_TRACK_TORQUE_ENTER_EFFORT;
    uint32_t torque_reverse_effort = axis == GIMBAL_AXIS_X ?
        GIMBAL_X_TRACK_TORQUE_REVERSE_EFFORT :
        GIMBAL_Y_TRACK_TORQUE_REVERSE_EFFORT;
    int8_t remembered_torque_sign = hold_torque_sign[axis];
    int8_t torque_sign = requested_torque_sign;
    if (remembered_torque_sign == 0) {
      hold_reverse_neutral_count[axis] = 0U;
      if (effort_abs < torque_enter_effort) {
        torque_sign = 0;
      }
    } else if (requested_torque_sign != remembered_torque_sign &&
               effort_abs < torque_reverse_effort) {
      /*
       * Keep the previous direction in memory while output is neutral.
       * Clearing it here made the next 5 ms cycle use the much smaller
       * entry threshold, effectively defeating reversal hysteresis.
       */
      if (axis == GIMBAL_AXIS_X &&
          hold_reverse_neutral_count[axis] <
              GIMBAL_X_TRACK_REVERSE_NEUTRAL_CYCLES) {
        ++hold_reverse_neutral_count[axis];
        torque_sign = 0;
      } else {
        hold_reverse_neutral_count[axis] = 0U;
      }
    } else {
      hold_reverse_neutral_count[axis] = 0U;
    }
    if (torque_sign == 0 || control_error == 0 ||
        (error_abs <= GIMBAL_FOC_DEADBAND_RAW && effort_abs < 8U)) {
      torque_sign = 0;
      duty_target = 0U;
    } else {
      uint32_t static_duty = axis == GIMBAL_AXIS_X ?
                             GIMBAL_X_TRACK_STATIC_DUTY :
                             GIMBAL_Y_TRACK_STATIC_DUTY;
      if (error_abs < GIMBAL_TRACK_STATIC_RAMP_RAW) {
        static_duty = (static_duty * error_abs) /
                      GIMBAL_TRACK_STATIC_RAMP_RAW;
      }
      duty_target = effort_abs + static_duty;
      if (duty_target > axis_hold_duty(axis)) {
        duty_target = axis_hold_duty(axis);
      }
    }
    if (torque_sign != 0) {
      hold_torque_sign[axis] = torque_sign;
    }
    if (hold_startup_cycles < GIMBAL_FOC_STARTUP_CYCLES) {
      uint32_t startup_limit = axis_hold_min_duty(axis) +
          ((axis_hold_duty(axis) - axis_hold_min_duty(axis)) *
           hold_startup_cycles) / GIMBAL_FOC_STARTUP_CYCLES;
      if (duty_target > startup_limit) duty_target = startup_limit;
    }
    uint32_t duty = slew_u32(hold_duty_ticks[axis], duty_target,
                             GIMBAL_FOC_DUTY_SLEW_TICKS);
    active_duty_ticks = duty;
    hold_duty_ticks[axis] = (uint16_t)duty;

    float field_angle = axis_electrical_angle(axis, raw);
    if (torque_sign != 0) {
      int8_t driven_sign =
          (int8_t)(torque_sign * axis_torque_polarity(axis));
      bool advance_field = driven_sign == hold_forward_raw_sign[axis];
      field_angle += advance_field ? FOC_HALF_PI : -FOC_HALF_PI;
    }
    hold_field_step[axis] =
        (uint8_t)((uint32_t)(foc_wrap_angle(field_angle) *
                             ((float)SINE_STEPS / FOC_TWO_PI)) % SINE_STEPS);
    update_foc_vector(hardware.timer, field_angle, duty);
  }
  if (hold_startup_cycles < GIMBAL_FOC_STARTUP_CYCLES) {
    ++hold_startup_cycles;
  }
}

void motor_pwm_hold_watchdog(void)
{
  if (!hold_enabled || hold_watchdog_deadline == 0U) return;
  uint32_t now = HAL_GetTick();
  if ((int32_t)(now - hold_watchdog_deadline) < 0) return;

  hold_enabled = false;
  all_motors_enabled = false;
  htim1.Instance->CCER &= ~ALL_PHASE_OUTPUTS;
  htim8.Instance->CCER &= ~ALL_PHASE_OUTPUTS;
  HAL_GPIO_WritePin(GIMBAL_X_EN_GPIO_Port, GIMBAL_X_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GIMBAL_Y_EN_GPIO_Port, GIMBAL_Y_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GIMBAL_X_STBY_GPIO_Port, GIMBAL_X_STBY_Pin,
                    GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GIMBAL_Y_STBY_GPIO_Port, GIMBAL_Y_STBY_Pin,
                    GPIO_PIN_RESET);
  board_set_boost(false);
  board_set_fault(true);
  hold_watchdog_deadline = 0U;
  hold_duty_ticks[GIMBAL_AXIS_X] = 0U;
  hold_duty_ticks[GIMBAL_AXIS_Y] = 0U;
  ++hold_watchdog_trips;
}

void motor_pwm_get_foc_status(gimbal_foc_status_t *status)
{
  if (status == NULL) return;
  status->enabled = hold_enabled;
  status->x_aligned = hold_commutation_ready[GIMBAL_AXIS_X];
  status->y_aligned = hold_commutation_ready[GIMBAL_AXIS_Y];
  status->x_error_raw = hold_error_raw[GIMBAL_AXIS_X];
  status->y_error_raw = hold_error_raw[GIMBAL_AXIS_Y];
  status->x_duty_ticks = hold_duty_ticks[GIMBAL_AXIS_X];
  status->y_duty_ticks = hold_duty_ticks[GIMBAL_AXIS_Y];
  status->x_direction = hold_forward_raw_sign[GIMBAL_AXIS_X];
  status->y_direction = hold_forward_raw_sign[GIMBAL_AXIS_Y];
  status->x_electric_offset = hold_commutation[GIMBAL_AXIS_X];
  status->y_electric_offset = hold_commutation[GIMBAL_AXIS_Y];
  status->x_pole_pairs = hold_pole_pairs[GIMBAL_AXIS_X];
  status->y_pole_pairs = hold_pole_pairs[GIMBAL_AXIS_Y];
  status->x_gravity_comp =
      (int16_t)lroundf(hold_integral_effort[GIMBAL_AXIS_X]);
  status->cycles = hold_cycle_count;
  status->watchdog_trips = hold_watchdog_trips;
}

void motor_pwm_gravity_clear(void)
{
  hold_integral_effort[GIMBAL_AXIS_X] = 0.0f;
  hold_integral_qualify_count[GIMBAL_AXIS_X] = 0U;
}
