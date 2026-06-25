#include "motor_pwm.h"
#include "as5600.h"
#include "main.h"
#include "tim.h"

/* TIM1/TIM8 的自动重装值为 4249，250 对应约 5.9% 占空比。 */
#define TEST_DUTY_TICKS          250U
#define TEST_START_STEP_MS        40U
#define TEST_MIN_STEP_MS          18U
#define TEST_ELECTRICAL_TURNS     12U
#define POSITION_DEADBAND         12
#define POSITION_MAX_STEPS       300U

#define PHASE_U 0U
#define PHASE_V 1U
#define PHASE_W 2U
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

static const uint8_t six_step_sequence[6][2] = {
  {PHASE_U, PHASE_V}, {PHASE_U, PHASE_W},
  {PHASE_V, PHASE_W}, {PHASE_V, PHASE_U},
  {PHASE_W, PHASE_U}, {PHASE_W, PHASE_V}
};

/*
 * 云台轴与 MCU 资源映射：
 * X 轴：编码器 PA15/PB7（I2C1）；电机 TIM8，PA0 使能，PC1 待机。
 * Y 轴：编码器 PB10/PB11（软件 I2C）；电机 TIM1，PA6 使能，PC0 待机。
 */
static axis_hardware_t get_axis_hardware(gimbal_axis_t axis)
{
  axis_hardware_t hardware;
  if (axis == GIMBAL_AXIS_X) {
    hardware.timer = &htim8;
    hardware.en_port = GIMBAL_X_EN_GPIO_Port;
    hardware.en_pin = GIMBAL_X_EN_Pin;
    hardware.stby_port = GIMBAL_X_STBY_GPIO_Port;
    hardware.stby_pin = GIMBAL_X_STBY_Pin;
    hardware.read_encoder = as5600_x_read_raw;
  } else {
    hardware.timer = &htim1;
    hardware.en_port = GIMBAL_Y_EN_GPIO_Port;
    hardware.en_pin = GIMBAL_Y_EN_Pin;
    hardware.stby_port = GIMBAL_Y_STBY_GPIO_Port;
    hardware.stby_pin = GIMBAL_Y_STBY_Pin;
    hardware.read_encoder = as5600_y_read_raw;
  }
  return hardware;
}

static HAL_StatusTypeDef start_timer(TIM_HandleTypeDef *timer)
{
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

static uint32_t phase_main_enable(uint8_t phase)
{
  static const uint32_t bits[3] = {TIM_CCER_CC1E, TIM_CCER_CC2E, TIM_CCER_CC3E};
  return bits[phase];
}

static uint32_t phase_n_enable(uint8_t phase)
{
  static const uint32_t bits[3] = {TIM_CCER_CC1NE, TIM_CCER_CC2NE, TIM_CCER_CC3NE};
  return bits[phase];
}

static uint32_t phase_channel(uint8_t phase)
{
  static const uint32_t channels[3] = {TIM_CHANNEL_1, TIM_CHANNEL_2, TIM_CHANNEL_3};
  return channels[phase];
}

/* 一相高侧 PWM、一相低侧导通、第三相悬空。 */
static void set_six_step(TIM_HandleTypeDef *timer, uint8_t high, uint8_t low)
{
  timer->Instance->CCER &= ~ALL_PHASE_OUTPUTS;
  set_all_compare(timer, 0U);
  __HAL_TIM_SET_COMPARE(timer, phase_channel(high), TEST_DUTY_TICKS);
  timer->Instance->CCER |= phase_main_enable(high) | phase_n_enable(high) |
                           phase_n_enable(low);
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

void motor_pwm_init(void)
{
  set_all_compare(&htim1, 0U);
  set_all_compare(&htim8, 0U);
  all_motors_enabled = false;
}

bool motor_pwm_set_enabled(bool turn_on)
{
  if (turn_on) {
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
  axis_hardware_t hardware = get_axis_hardware(axis);
  if (!start_axis(&hardware)) return false;

  bool run_ok = true;
  for (uint8_t turn = 0U; turn < TEST_ELECTRICAL_TURNS && run_ok; ++turn) {
    uint32_t reduction = (uint32_t)turn * 2U;
    uint32_t step_delay = reduction < (TEST_START_STEP_MS - TEST_MIN_STEP_MS) ?
                          TEST_START_STEP_MS - reduction : TEST_MIN_STEP_MS;
    for (uint8_t step = 0U; step < 6U; ++step) {
      set_six_step(hardware.timer, six_step_sequence[step][0],
                   six_step_sequence[step][1]);
      HAL_Delay(step_delay);
      if (HAL_GPIO_ReadPin(hardware.en_port, hardware.en_pin) == GPIO_PIN_RESET) {
        run_ok = false;
        break;
      }
    }
  }
  stop_axis(&hardware);
  return run_ok;
}

bool motor_pwm_set_position(gimbal_axis_t axis, uint16_t target_raw,
                            uint16_t *final_raw)
{
  axis_hardware_t hardware = get_axis_hardware(axis);
  uint16_t current = read_encoder_quiet(&hardware);
  if (target_raw > 4095U || current == AS5600_READ_ERROR) return false;
  if (!start_axis(&hardware)) return false;

  uint8_t commutation = 0U;
  set_six_step(hardware.timer, six_step_sequence[0][0], six_step_sequence[0][1]);
  HAL_Delay(60U);
  uint16_t calibration_start = read_encoder_quiet(&hardware);
  if (calibration_start == AS5600_READ_ERROR) {
    stop_axis(&hardware);
    return false;
  }

  /* 自动判断正向相序对应编码器增大还是减小，避免硬编码安装方向。 */
  for (uint8_t i = 0U; i < 6U; ++i) {
    commutation = (uint8_t)((commutation + 1U) % 6U);
    set_six_step(hardware.timer, six_step_sequence[commutation][0],
                 six_step_sequence[commutation][1]);
    HAL_Delay(35U);
  }
  current = read_encoder_quiet(&hardware);
  int16_t calibration_delta = angle_error(current, calibration_start);
  if (current == AS5600_READ_ERROR ||
      (calibration_delta > -4 && calibration_delta < 4)) {
    stop_axis(&hardware);
    return false;
  }

  int8_t forward_raw_sign = calibration_delta > 0 ? 1 : -1;
  uint8_t stable_samples = 0U;
  bool reached = false;

  for (uint16_t iteration = 0U; iteration < POSITION_MAX_STEPS; ++iteration) {
    current = read_encoder_quiet(&hardware);
    if (current == AS5600_READ_ERROR ||
        HAL_GPIO_ReadPin(hardware.en_port, hardware.en_pin) == GPIO_PIN_RESET) break;

    int16_t error = angle_error(target_raw, current);
    if (error >= -POSITION_DEADBAND && error <= POSITION_DEADBAND) {
      if (++stable_samples >= 3U) {
        reached = true;
        break;
      }
      HAL_Delay(10U);
      continue;
    }

    stable_samples = 0U;
    int8_t requested_raw_sign = error > 0 ? 1 : -1;
    commutation = requested_raw_sign == forward_raw_sign ?
                  (uint8_t)((commutation + 1U) % 6U) :
                  (uint8_t)((commutation + 5U) % 6U);
    set_six_step(hardware.timer, six_step_sequence[commutation][0],
                 six_step_sequence[commutation][1]);
    HAL_Delay(20U);
  }

  if (final_raw != NULL) *final_raw = read_encoder_quiet(&hardware);
  stop_axis(&hardware);
  return reached;
}
