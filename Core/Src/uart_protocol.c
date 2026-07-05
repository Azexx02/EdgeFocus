#include "uart_protocol.h"
#include "as5600.h"
#include "board_config.h"
#include "current_sample.h"
#include "gimbal_config.h"
#include "gimbal_storage.h"
#include "motor_pwm.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_LINE_SIZE 64U
#define DEFERRED_RX_SIZE 128U
#define PROTOCOL_VERSION "1.0"
#define X_SEGMENT_DEG 5U
#define XY_TRAJECTORY_STEP_DEG 10U

static char rx_line[RX_LINE_SIZE];
static uint8_t rx_length;
static bool rx_discard_line;
static uint8_t deferred_rx[DEFERRED_RX_SIZE];
static volatile uint8_t deferred_rx_read;
static volatile uint8_t deferred_rx_write;
static uint32_t uart_error_count;
static volatile uint8_t protocol_busy;
static uint8_t uart_rx_irq_byte;

void uart_protocol_set_busy(uint8_t busy)
{
  protocol_busy = busy ? 1U : 0U;
}

uint8_t uart_protocol_is_busy(void)
{
  return protocol_busy;
}

static bool command_allowed_while_busy(const char *command)
{
  return strcmp(command, "PING") == 0 ||
         strcmp(command, "VER") == 0 ||
         strcmp(command, "HELP") == 0 ||
         strncmp(command, "ECHO ", 5U) == 0 ||
         strcmp(command, "STOP") == 0 ||
         strcmp(command, "SAFE") == 0 ||
         strcmp(command, "STAT") == 0 ||
         strcmp(command, "ANGLE") == 0 ||
         strcmp(command, "FAULT") == 0 ||
         strcmp(command, "FOCSTAT") == 0 ||
         strcmp(command, "HOMEINFO") == 0;
}

void uart_protocol_send(const char *text)
{
  HAL_UART_Transmit(&huart4, (const uint8_t *)text, (uint16_t)strlen(text), 100U);
}

#if !GIMBAL_CONTROL_ENABLE
static void send_control_disabled(void)
{
  uart_protocol_send("ERR,CONTROL_DISABLED\r\n");
}
#endif

static bool parse_axis(char c, gimbal_axis_t *axis)
{
  if (c == 'X') {
    *axis = GIMBAL_AXIS_X;
    return true;
  }
  if (c == 'Y') {
    *axis = GIMBAL_AXIS_Y;
    return true;
  }
  return false;
}

static bool parse_u16(const char *text, uint16_t max_value, uint16_t *value)
{
  char *end;
  unsigned long parsed = strtoul(text, &end, 10);
  if (*text == '\0' || *end != '\0' || parsed > max_value) return false;
  *value = (uint16_t)parsed;
  return true;
}

static void enter_safe_state(void)
{
  motor_pwm_hold_enable(false);
  motor_pwm_set_enabled(false);
  board_set_boost(false);
  board_set_dac_mv(GIMBAL_IDLE_DAC_MV, GIMBAL_IDLE_DAC_MV);
  board_set_fault(false);
}

static const char *limit_event_name(void)
{
  switch (motor_pwm_get_limit_event()) {
    case GIMBAL_LIMIT_X_MIN: return "X_MIN";
    case GIMBAL_LIMIT_X_MAX: return "X_MAX";
    case GIMBAL_LIMIT_X_BLOCKED: return "X_BLOCKED";
    default: return "NONE";
  }
}

static uint16_t read_axis_raw_retry(gimbal_axis_t axis)
{
  for (uint8_t attempt = 0U; attempt < 4U; ++attempt) {
    uint16_t raw = motor_pwm_read_raw(axis);
    if (raw != AS5600_READ_ERROR) return raw;
    HAL_Delay(2U);
  }
  return AS5600_READ_ERROR;
}

static bool move_axis_to_deg(gimbal_axis_t axis, uint16_t target_deg,
                             uint16_t max_step_deg, uint16_t *final_deg,
                             uint16_t *final_raw)
{
  if (final_deg != NULL) *final_deg = UINT16_MAX;
  if (final_raw != NULL) *final_raw = AS5600_READ_ERROR;
  uint16_t current_raw = read_axis_raw_retry(axis);
  uint16_t current_deg;
  if (!motor_pwm_calibration_is_valid(axis) ||
      !motor_pwm_raw_to_deg(axis, current_raw, &current_deg) ||
      target_deg > motor_pwm_get_limit_deg(axis)) {
    return false;
  }

  if (max_step_deg == 0U || axis == GIMBAL_AXIS_Y) {
    max_step_deg = motor_pwm_get_limit_deg(axis);
  }

  if (current_deg == target_deg) {
    if (final_deg != NULL) *final_deg = current_deg;
    if (final_raw != NULL) *final_raw = current_raw;
    (void)motor_pwm_hold_set_target(axis, target_deg);
    return true;
  }

  while (current_deg != target_deg) {
    uint16_t next_deg = target_deg;
    if (target_deg > current_deg &&
        (uint16_t)(target_deg - current_deg) > max_step_deg) {
      next_deg = (uint16_t)(current_deg + max_step_deg);
    } else if (current_deg > target_deg &&
               (uint16_t)(current_deg - target_deg) > max_step_deg) {
      next_deg = (uint16_t)(current_deg - max_step_deg);
    }

    uint16_t next_raw;
    if (motor_pwm_deg_to_raw(axis, next_deg, &next_raw)) {
      int32_t raw_delta = (int32_t)next_raw - (int32_t)current_raw;
      if (raw_delta > 2047) raw_delta -= 4096;
      if (raw_delta < -2048) raw_delta += 4096;
      if (raw_delta >= -8 && raw_delta <= 8) {
        if (final_deg != NULL) *final_deg = next_deg;
        if (final_raw != NULL) *final_raw = current_raw;
        current_deg = next_deg;
        continue;
      }
    }

    board_set_boost(true);
    motor_pwm_select_position_power(axis, next_deg >= current_deg ? 1 : -1);
    bool ok = motor_pwm_set_position_deg(axis, next_deg, final_deg, final_raw);
    motor_pwm_restore_idle_power();
    board_set_boost(false);
    if (!ok) return false;

    current_deg = *final_deg;
    if (current_deg == next_deg) continue;
    if ((current_deg > next_deg ? current_deg - next_deg :
         next_deg - current_deg) <= 1U) {
      current_deg = next_deg;
    }
  }

  (void)motor_pwm_hold_set_target(axis, target_deg);
  return true;
}

static bool parse_xy(const char *text, bool with_speed,
                     uint16_t *x, uint16_t *y, uint16_t *speed)
{
  unsigned int xv;
  unsigned int yv;
  unsigned int sv = 0U;
  char extra;
  int count = with_speed
                  ? sscanf(text, "%u %u %u %c", &xv, &yv, &sv, &extra)
                  : sscanf(text, "%u %u %c", &xv, &yv, &extra);
  if (count != (with_speed ? 3 : 2) ||
      xv > motor_pwm_get_limit_deg(GIMBAL_AXIS_X) ||
      yv > motor_pwm_get_limit_deg(GIMBAL_AXIS_Y) ||
      (with_speed && (sv < 1U || sv > 90U))) {
    return false;
  }
  *x = (uint16_t)xv;
  *y = (uint16_t)yv;
  *speed = (uint16_t)sv;
  return true;
}

static void execute_xy_command(const char *command, bool trajectory)
{
#if GIMBAL_CONTROL_ENABLE
  char response[112];
  if (motor_pwm_hold_is_enabled()) {
    uart_protocol_send("ERR,HOLD_ACTIVE\r\n");
    return;
  }
  uint16_t target_x;
  uint16_t target_y;
  uint16_t speed;
  const char *values = command + (trajectory ? 8U : 7U);
  if (!parse_xy(values, trajectory, &target_x, &target_y, &speed)) {
    uart_protocol_send(trajectory ? "ERR,BAD_MOVE_XY\r\n" :
                                    "ERR,BAD_SET_XY\r\n");
    return;
  }
  if (!motor_pwm_calibration_is_valid(GIMBAL_AXIS_X) ||
      !motor_pwm_calibration_is_valid(GIMBAL_AXIS_Y)) {
    uart_protocol_send("ERR,NOT_HOMED\r\n");
    return;
  }

  uint16_t x_raw = motor_pwm_read_raw(GIMBAL_AXIS_X);
  uint16_t y_raw = motor_pwm_read_raw(GIMBAL_AXIS_Y);
  uint16_t start_x;
  uint16_t start_y;
  if (!motor_pwm_raw_to_deg(GIMBAL_AXIS_X, x_raw, &start_x) ||
      !motor_pwm_raw_to_deg(GIMBAL_AXIS_Y, y_raw, &start_y)) {
    uart_protocol_send("ERR,ANGLE_READ\r\n");
    return;
  }

  snprintf(response, sizeof(response), "%s,XY_START,X=%u,Y=%u%s%u\r\n",
           trajectory ? "MOVE" : "SET", target_x, target_y,
           trajectory ? ",SPEED=" : ",MODE=", trajectory ? speed : 0U);
  uart_protocol_send(response);

  uint16_t steps = 1U;
  int32_t dx = (int32_t)target_x - (int32_t)start_x;
  int32_t dy = (int32_t)target_y - (int32_t)start_y;
  if (trajectory) {
    if (dy > 180) dy -= 360;
    if (dy < -180) dy += 360;
    uint32_t span_x = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t span_y = (uint32_t)(dy < 0 ? -dy : dy);
    uint32_t span = span_x > span_y ? span_x : span_y;
    steps = (uint16_t)((span + XY_TRAJECTORY_STEP_DEG - 1U) /
                       XY_TRAJECTORY_STEP_DEG);
    if (steps == 0U) steps = 1U;
  }

  uint16_t final_x = start_x;
  uint16_t final_y = start_y;
  for (uint16_t i = 1U; i <= steps; ++i) {
    int32_t next_x = (int32_t)start_x + dx * i / steps;
    int32_t next_y = (int32_t)start_y + dy * i / steps;
    while (next_y < 0) next_y += 360;
    while (next_y >= 360) next_y -= 360;

    if (!move_axis_to_deg(GIMBAL_AXIS_X, (uint16_t)next_x,
                          X_SEGMENT_DEG, &final_x, &x_raw)) {
      enter_safe_state();
      snprintf(response, sizeof(response), "ERR,%s_X,E=%u,LIMIT=%s,RAW=%u\r\n",
               trajectory ? "MOVE" : "SET",
               motor_pwm_get_position_error(GIMBAL_AXIS_X),
               limit_event_name(), motor_pwm_get_limit_event_raw());
      uart_protocol_send(response);
      return;
    }
    if (!move_axis_to_deg(GIMBAL_AXIS_Y, (uint16_t)next_y,
                          0U, &final_y, &y_raw)) {
      enter_safe_state();
      snprintf(response, sizeof(response), "ERR,%s_Y,E=%u,LIMIT=%s,RAW=%u\r\n",
               trajectory ? "MOVE" : "SET",
               motor_pwm_get_position_error(GIMBAL_AXIS_Y),
               limit_event_name(), motor_pwm_get_limit_event_raw());
      uart_protocol_send(response);
      return;
    }
    if (trajectory && i < steps) {
      uint32_t delay_ms = 10000U / speed;
      if (delay_ms > 500U) delay_ms = 500U;
      HAL_Delay(delay_ms);
    }
  }

  snprintf(response, sizeof(response), "%s,XY_OK,X=%u,Y=%u,XR=%u,YR=%u\r\n",
           trajectory ? "MOVE" : "SET", final_x, final_y, x_raw, y_raw);
  uart_protocol_send(response);
#else
  (void)command;
  (void)trajectory;
  send_control_disabled();
#endif
}

static void send_angle_response(void)
{
  char response[128];
  uint16_t x_raw = motor_pwm_read_raw(GIMBAL_AXIS_X);
  uint16_t y_raw = motor_pwm_read_raw(GIMBAL_AXIS_Y);
  uint16_t x_deg = UINT16_MAX;
  uint16_t y_deg = UINT16_MAX;

  (void)motor_pwm_raw_to_deg(GIMBAL_AXIS_X, x_raw, &x_deg);
  (void)motor_pwm_raw_to_deg(GIMBAL_AXIS_Y, y_raw, &y_deg);
  snprintf(response, sizeof(response),
           "ANGLE,X=%u,Y=%u,XD=%u,YD=%u,XHOME=%u,YHOME=%u,EN=%u\r\n",
           x_raw, y_raw, x_deg, y_deg,
           motor_pwm_get_home(GIMBAL_AXIS_X),
           motor_pwm_get_home(GIMBAL_AXIS_Y),
           board_get_enable_input() ? 1U : 0U);
  uart_protocol_send(response);
}

static void send_home_info_response(void)
{
  char response[160];
  gimbal_axis_home_t x;
  gimbal_axis_home_t y;
  bool x_ok = motor_pwm_get_home_info(GIMBAL_AXIS_X, &x);
  bool y_ok = motor_pwm_get_home_info(GIMBAL_AXIS_Y, &y);

  snprintf(response, sizeof(response),
           "HOMEINFO,XOK=%u,XE=%u,XLO=%u,XHI=%u,XCR=%u,XC=%u,XTR=%u,YOK=%u,YE=%u,YLO=%u,YHI=%u,YCR=%u,YC=%u,YTR=%u,HOLD=%u\r\n",
           x_ok ? 1U : 0U, motor_pwm_get_home_error(GIMBAL_AXIS_X),
           x.low_raw, x.high_raw, x.center_raw, x.center_deg, x.travel_deg,
           y_ok ? 1U : 0U, motor_pwm_get_home_error(GIMBAL_AXIS_Y),
           y.low_raw, y.high_raw, y.center_raw, y.center_deg, y.travel_deg,
           motor_pwm_hold_is_enabled() ? 1U : 0U);
  uart_protocol_send(response);
}

static void execute_home_command(const char *command)
{
  char response[64];
  gimbal_axis_t axis;
  if (strcmp(command, "HOME ALL") == 0) {
    uint16_t x_home;
    uint16_t y_home;
    if (!motor_pwm_home_from_encoder(GIMBAL_AXIS_X, &x_home) ||
        !motor_pwm_home_from_encoder(GIMBAL_AXIS_Y, &y_home)) {
      uart_protocol_send("ERR,HOME_READ\r\n");
      return;
    }
    snprintf(response, sizeof(response), "OK,HOME,X=%u,Y=%u\r\n",
             x_home, y_home);
    uart_protocol_send(response);
    return;
  }

  if (strncmp(command, "HOME ", 5U) != 0 ||
      command[6] != '\0' ||
      !parse_axis(command[5], &axis)) {
    uart_protocol_send("ERR,BAD_HOME\r\n");
    return;
  }

  uint16_t home_raw;
  if (!motor_pwm_home_from_encoder(axis, &home_raw)) {
    uart_protocol_send("ERR,HOME_READ\r\n");
    return;
  }
  snprintf(response, sizeof(response), "OK,HOME,%c=%u,LIMIT=%u\r\n",
           command[5], home_raw, motor_pwm_get_limit_deg(axis));
  uart_protocol_send(response);
}

static void execute_position_command(const char *command)
{
#if GIMBAL_CONTROL_ENABLE
  char response[64];
  if (motor_pwm_hold_is_enabled()) {
    uart_protocol_send("ERR,HOLD_ACTIVE\r\n");
    return;
  }
  uint16_t target;
  if (!parse_u16(command + 6, 4095U, &target)) {
    uart_protocol_send("ERR,BAD_POSITION\r\n");
    return;
  }

  gimbal_axis_t axis = command[4] == 'X' ? GIMBAL_AXIS_X : GIMBAL_AXIS_Y;
  char axis_name = command[4];
  uint16_t final_raw = AS5600_READ_ERROR;
  uint16_t target_deg;
  if (!motor_pwm_calibration_is_valid(axis) ||
      !motor_pwm_raw_to_deg(axis, target, &target_deg)) {
    uart_protocol_send("ERR,NOT_HOMED_OR_OUT_OF_LIMIT\r\n");
    return;
  }

  board_set_boost(true);
  uint16_t current_raw = motor_pwm_read_raw(axis);
  uint16_t current_deg = target_deg;
  (void)motor_pwm_raw_to_deg(axis, current_raw, &current_deg);
  motor_pwm_select_position_power(axis,
                                  target_deg >= current_deg ? 1 : -1);
  snprintf(response, sizeof(response), "POS,%c_START,TARGET=%u\r\n",
           axis_name, target);
  uart_protocol_send(response);

  bool ok = motor_pwm_set_position(axis, target, &final_raw);
  motor_pwm_restore_idle_power();
  board_set_boost(false);
  if (ok) {
    snprintf(response, sizeof(response), "POS,%c=%u,TARGET=%u\r\n",
             axis_name, final_raw, target);
  } else {
    snprintf(response, sizeof(response), "ERR,%c_POS,E=%u,RAW=%u\r\n",
             axis_name, motor_pwm_get_position_error(axis), final_raw);
  }
  uart_protocol_send(response);
#else
  (void)command;
  send_control_disabled();
#endif
}

static void execute_degree_command(const char *command)
{
#if GIMBAL_CONTROL_ENABLE
  char response[80];
  if (motor_pwm_hold_is_enabled()) {
    uart_protocol_send("ERR,HOLD_ACTIVE\r\n");
    return;
  }
  gimbal_axis_t axis;
  if (!parse_axis(command[4], &axis)) {
    uart_protocol_send("ERR,BAD_AXIS\r\n");
    return;
  }
  if (!motor_pwm_calibration_is_valid(axis)) {
    uart_protocol_send("ERR,NOT_HOMED\r\n");
    return;
  }

  uint16_t target_deg;
  if (!parse_u16(command + 6, motor_pwm_get_limit_deg(axis), &target_deg)) {
    uart_protocol_send("ERR,BAD_DEGREE\r\n");
    return;
  }

  uint16_t target_raw;
  uint16_t final_deg = UINT16_MAX;
  uint16_t final_raw = AS5600_READ_ERROR;
  if (!motor_pwm_deg_to_raw(axis, target_deg, &target_raw)) {
    uart_protocol_send("ERR,BAD_DEGREE\r\n");
    return;
  }

  snprintf(response, sizeof(response),
           "DEG,%c_START,TARGET=%u,RAW=%u,LIMIT=%u\r\n",
           command[4], target_deg, target_raw, motor_pwm_get_limit_deg(axis));
  uart_protocol_send(response);

  bool ok = move_axis_to_deg(axis, target_deg,
                             axis == GIMBAL_AXIS_X ? X_SEGMENT_DEG : 0U,
                             &final_deg, &final_raw);
  if (ok && final_deg != UINT16_MAX && final_raw != AS5600_READ_ERROR) {
    snprintf(response, sizeof(response), "DEG,%c=%u,RAW=%u,TARGET=%u\r\n",
             command[4], final_deg, final_raw, target_deg);
  } else {
    snprintf(response, sizeof(response),
             "ERR,%c_DEG,E=%u,DEG=%u,RAW=%u,LIMIT=%s,LRAW=%u\r\n",
             command[4],
             motor_pwm_get_position_error(axis) == 0U ? 1U :
             motor_pwm_get_position_error(axis),
             final_deg, final_raw, limit_event_name(),
             motor_pwm_get_limit_event_raw());
  }
  uart_protocol_send(response);
#else
  (void)command;
  send_control_disabled();
#endif
}

static void execute_mark_command(const char *command)
{
  char response[96];
  gimbal_axis_t axis;
  if (strncmp(command, "MARK ", 5U) != 0 ||
      !parse_axis(command[5], &axis) ||
      command[6] != ' ') {
    uart_protocol_send("ERR,BAD_MARK\r\n");
    return;
  }

  bool low_end;
  if (strcmp(command + 7, "MIN") == 0 || strcmp(command + 7, "LOW") == 0) {
    low_end = true;
  } else if (strcmp(command + 7, "MAX") == 0 ||
             strcmp(command + 7, "HIGH") == 0) {
    low_end = false;
  } else {
    uart_protocol_send("ERR,BAD_MARK\r\n");
    return;
  }

  uint16_t raw;
  if (!motor_pwm_mark_manual_limit(axis, low_end, &raw)) {
    uart_protocol_send("ERR,MARK_READ\r\n");
    return;
  }

  snprintf(response, sizeof(response), "OK,MARK,%c,%s=%u\r\n",
           command[5], low_end ? "MIN" : "MAX", raw);
  uart_protocol_send(response);
  send_home_info_response();
}

static void execute_markraw_command(const char *command)
{
  char response[96];
  gimbal_axis_t axis;
  if (strncmp(command, "MARKRAW ", 8U) != 0 ||
      !parse_axis(command[8], &axis) ||
      command[9] != ' ') {
    uart_protocol_send("ERR,BAD_MARKRAW\r\n");
    return;
  }

  const char *end_name = command + 10;
  bool low_end;
  const char *value_text;
  if (strncmp(end_name, "MIN ", 4U) == 0 ||
      strncmp(end_name, "LOW ", 4U) == 0) {
    low_end = true;
    value_text = end_name + 4;
  } else if (strncmp(end_name, "MAX ", 4U) == 0 ||
             strncmp(end_name, "HIGH ", 5U) == 0) {
    low_end = false;
    value_text = end_name + (end_name[1] == 'A' ? 4U : 5U);
  } else {
    uart_protocol_send("ERR,BAD_MARKRAW\r\n");
    return;
  }

  uint16_t raw;
  if (!parse_u16(value_text, 4095U, &raw) ||
      !motor_pwm_set_manual_limit_raw(axis, low_end, raw)) {
    uart_protocol_send("ERR,BAD_MARKRAW\r\n");
    return;
  }

  snprintf(response, sizeof(response), "OK,MARKRAW,%c,%s=%u\r\n",
           command[8], low_end ? "MIN" : "MAX", raw);
  uart_protocol_send(response);
  send_home_info_response();
}

static void execute_center_command(const char *command)
{
#if GIMBAL_CONTROL_ENABLE
  char response[96];
  if (motor_pwm_hold_is_enabled()) {
    uart_protocol_send("ERR,HOLD_ACTIVE\r\n");
    return;
  }
  gimbal_axis_t axis;
  if (strncmp(command, "CENTER ", 7U) != 0 ||
      command[8] != '\0' ||
      !parse_axis(command[7], &axis)) {
    uart_protocol_send("ERR,BAD_CENTER\r\n");
    return;
  }

  gimbal_axis_home_t home;
  if (!motor_pwm_get_home_info(axis, &home)) {
    uart_protocol_send("ERR,NOT_HOMED\r\n");
    return;
  }

  uint16_t final_raw = AS5600_READ_ERROR;
  bool ok = motor_pwm_move_to_marked_center(axis, &final_raw);
  if (ok) (void)motor_pwm_hold_set_target(axis, home.center_deg);
  snprintf(response, sizeof(response), ok ? "CENTER,%c_OK,DEG=%u,RAW=%u\r\n" :
           "ERR,CENTER_%c,DEG=%u,RAW=%u\r\n",
           command[7], home.center_deg, final_raw);
  uart_protocol_send(response);
#else
  (void)command;
  send_control_disabled();
#endif
}

static void execute(const char *command)
{
  char response[160];

  if (protocol_busy && !command_allowed_while_busy(command)) {
    uart_protocol_send("ERR,BUSY\r\n");
    return;
  }

  if (strcmp(command, "PING") == 0) {
    uart_protocol_send("OK,G431\r\n");
  } else if (strcmp(command, "VER") == 0) {
    uart_protocol_send("VER,PROTO=1.0,FW=G431-GIMBAL,CAPS=STOP|SET_XY|MOVE_XY|FAULT|X_SEGMENT|Y_CONTINUOUS|LIMIT_PROTECT|FOC_HOLD|FOCSTAT\r\n");
  } else if (strcmp(command, "HELP") == 0) {
    uart_protocol_send("CMDS,VER,PING,STOP,STAT,ANGLE,FAULT,FOCSTAT,SET_XY,MOVE_XY,DEG,CENTER,HOLD,HOMEINFO,CALIBRATION,MAINTENANCE\r\n");
  } else if (strncmp(command, "ECHO ", 5U) == 0) {
    uart_protocol_send("ECHO,");
    uart_protocol_send(command + 5);
    uart_protocol_send("\r\n");
  } else if (strcmp(command, "SAFE") == 0) {
    enter_safe_state();
    uart_protocol_send("OK,SAFE\r\n");
  } else if (strcmp(command, "STOP") == 0) {
    enter_safe_state();
    uart_protocol_send("OK,STOP\r\n");
  } else if (strcmp(command, "FAULT") == 0) {
    snprintf(response, sizeof(response),
             "FAULT,XHOME_E=%u,YHOME_E=%u,XPOS_E=%u,YPOS_E=%u,IX=%u,IY=%u,LIMIT=%s,LRAW=%u\r\n",
             motor_pwm_get_home_error(GIMBAL_AXIS_X),
             motor_pwm_get_home_error(GIMBAL_AXIS_Y),
             motor_pwm_get_position_error(GIMBAL_AXIS_X),
             motor_pwm_get_position_error(GIMBAL_AXIS_Y),
             current_get_x_raw(), current_get_y_raw(),
             limit_event_name(), motor_pwm_get_limit_event_raw());
    uart_protocol_send(response);
  } else if (strcmp(command, "FOCSTAT") == 0) {
    gimbal_foc_status_t foc;
    motor_pwm_get_foc_status(&foc);
    snprintf(response, sizeof(response),
             "FOCSTAT,EN=%u,XALIGN=%u,YALIGN=%u,XE=%u,YE=%u,XDUTY=%u,YDUTY=%u,XDIR=%d,YDIR=%d,XOFF=%u,YOFF=%u,XPP=%u,YPP=%u,CYC=%lu,WD=%lu,UARTERR=%lu,PERIOD=%u\r\n",
             foc.enabled ? 1U : 0U, foc.x_aligned ? 1U : 0U,
             foc.y_aligned ? 1U : 0U, foc.x_error_raw, foc.y_error_raw,
             foc.x_duty_ticks, foc.y_duty_ticks,
             foc.x_direction, foc.y_direction,
             foc.x_electric_offset, foc.y_electric_offset,
             foc.x_pole_pairs, foc.y_pole_pairs,
             (unsigned long)foc.cycles, (unsigned long)foc.watchdog_trips,
             (unsigned long)uart_error_count, GIMBAL_FOC_PERIOD_MS);
    uart_protocol_send(response);
  } else if (strcmp(command, "STAT") == 0) {
    snprintf(response, sizeof(response),
           "STAT,SAFE=%u,BOOST=%u,MOTOR=%u,HOLD=%u,XHOME=%u,YHOME=%u,CAL=%u,XT=%u,YT=%u,EN=%u,IX=%u,IY=%u,XLIM=%u,YLIM=%u\r\n",
             GIMBAL_CONTROL_ENABLE ? 0U : 1U,
             board_get_boost() ? 1U : 0U,
             motor_pwm_is_enabled() ? 1U : 0U,
             motor_pwm_hold_is_enabled() ? 1U : 0U,
             motor_pwm_calibration_is_valid(GIMBAL_AXIS_X) ? 1U : 0U,
             motor_pwm_calibration_is_valid(GIMBAL_AXIS_Y) ? 1U : 0U,
             (gimbal_storage_is_valid() &&
              motor_pwm_calibration_is_valid(GIMBAL_AXIS_X) &&
              motor_pwm_calibration_is_valid(GIMBAL_AXIS_Y)) ? 1U : 0U,
             motor_pwm_hold_get_target(GIMBAL_AXIS_X),
             motor_pwm_hold_get_target(GIMBAL_AXIS_Y),
             board_get_enable_input() ? 1U : 0U,
             current_get_x_raw(), current_get_y_raw(),
             motor_pwm_get_limit_deg(GIMBAL_AXIS_X),
             motor_pwm_get_limit_deg(GIMBAL_AXIS_Y));
    uart_protocol_send(response);
  } else if (strcmp(command, "ANGLE") == 0) {
    send_angle_response();
  } else if (strcmp(command, "LIMIT") == 0) {
    snprintf(response, sizeof(response), "LIMIT,X=%u,Y=%u\r\n",
             motor_pwm_get_limit_deg(GIMBAL_AXIS_X),
             motor_pwm_get_limit_deg(GIMBAL_AXIS_Y));
    uart_protocol_send(response);
  } else if (strcmp(command, "HOMEINFO") == 0) {
    send_home_info_response();
  } else if (strcmp(command, "CALSAVE") == 0) {
    motor_pwm_hold_enable(false);
    motor_pwm_set_enabled(false);
    board_set_boost(false);
    uart_protocol_send(gimbal_storage_save() ? "OK,CALSAVE\r\n" :
                       "ERR,CALSAVE,NEED_XY_LIMITS\r\n");
  } else if (strcmp(command, "CALLOAD") == 0) {
    motor_pwm_hold_enable(false);
    motor_pwm_set_enabled(false);
    board_set_boost(false);
    uart_protocol_send(gimbal_storage_load() ? "OK,CALLOAD\r\n" :
                       "ERR,CALLOAD,EMPTY_OR_INVALID\r\n");
  } else if (strcmp(command, "CALCLEAR") == 0) {
    motor_pwm_hold_enable(false);
    motor_pwm_set_enabled(false);
    board_set_boost(false);
    if (gimbal_storage_clear()) {
      motor_pwm_clear_calibration();
      uart_protocol_send("OK,CALCLEAR\r\n");
    } else {
      uart_protocol_send("ERR,CALCLEAR\r\n");
    }
  } else if (strncmp(command, "MARK ", 5U) == 0) {
    execute_mark_command(command);
  } else if (strncmp(command, "MARKRAW ", 8U) == 0) {
    execute_markraw_command(command);
  } else if (strncmp(command, "CENTER ", 7U) == 0) {
    uart_protocol_set_busy(1U);
    execute_center_command(command);
    uart_protocol_set_busy(0U);
  } else if (strcmp(command, "AUTOHOME") == 0) {
#if GIMBAL_CONTROL_ENABLE
    uart_protocol_send("AUTOHOME,START\r\n");
    uart_protocol_set_busy(1U);
    bool ok = motor_pwm_auto_home_all();
    uart_protocol_set_busy(0U);
    uart_protocol_send(ok ? "AUTOHOME,OK\r\n" : "ERR,AUTOHOME\r\n");
    send_home_info_response();
#else
    send_control_disabled();
#endif
  } else if ((strcmp(command, "AUTOHOME X") == 0) ||
             (strcmp(command, "AUTOHOME Y") == 0)) {
#if GIMBAL_CONTROL_ENABLE
    gimbal_axis_t axis = command[9] == 'X' ? GIMBAL_AXIS_X : GIMBAL_AXIS_Y;
    snprintf(response, sizeof(response), "AUTOHOME,%c_START\r\n", command[9]);
    uart_protocol_send(response);
    uart_protocol_set_busy(1U);
    bool ok = motor_pwm_auto_home_axis(axis, NULL);
    uart_protocol_set_busy(0U);
    if (ok) {
      snprintf(response, sizeof(response), "AUTOHOME,%c_OK\r\n", command[9]);
    } else {
      snprintf(response, sizeof(response), "ERR,AUTOHOME_%c,E=%u\r\n",
               command[9], motor_pwm_get_home_error(axis));
    }
    uart_protocol_send(response);
    send_home_info_response();
#else
    send_control_disabled();
#endif
  } else if (strcmp(command, "HOLD 1") == 0) {
#if GIMBAL_CONTROL_ENABLE
#if GIMBAL_FOC_HOLD_ENABLE
    if (!motor_pwm_hold_is_enabled()) motor_pwm_hold_enable(true);
    if (motor_pwm_hold_is_enabled()) {
      snprintf(response, sizeof(response), "OK,HOLD_ON,X=%u,Y=%u\r\n",
               motor_pwm_hold_get_target(GIMBAL_AXIS_X),
               motor_pwm_hold_get_target(GIMBAL_AXIS_Y));
      uart_protocol_send(response);
    } else {
      uart_protocol_send("ERR,NOT_HOMED_OR_ANGLE\r\n");
    }
#else
    uart_protocol_send("ERR,HOLD_UNAVAILABLE\r\n");
#endif
#else
    send_control_disabled();
#endif
  } else if (strcmp(command, "HOLD 0") == 0) {
    motor_pwm_hold_enable(false);
    uart_protocol_send("OK,HOLD_OFF\r\n");
  } else if (strncmp(command, "HOME ", 5U) == 0) {
    execute_home_command(command);
  } else if (strcmp(command, "BOOST 1") == 0) {
#if GIMBAL_CONTROL_ENABLE
    board_set_boost(true);
    uart_protocol_send("OK,BOOST_ON\r\n");
#else
    send_control_disabled();
#endif
  } else if (strcmp(command, "BOOST 0") == 0) {
    board_set_boost(false);
    uart_protocol_send("OK,BOOST_OFF\r\n");
  } else if (strcmp(command, "MOTOR 1") == 0) {
#if GIMBAL_CONTROL_ENABLE
    uart_protocol_send(motor_pwm_set_enabled(true) ? "OK,MOTOR_ON\r\n" : "ERR,MOTOR_START\r\n");
#else
    send_control_disabled();
#endif
  } else if (strcmp(command, "MOTOR 0") == 0) {
    motor_pwm_set_enabled(false);
    uart_protocol_send("OK,MOTOR_OFF\r\n");
  } else if (strcmp(command, "TEST X") == 0) {
#if GIMBAL_CONTROL_ENABLE
    board_set_boost(true);
    uart_protocol_send("TEST,X_START\r\n");
    uart_protocol_set_busy(1U);
    uart_protocol_send(motor_pwm_test(GIMBAL_AXIS_X) ? "TEST,X_END\r\n" : "ERR,X_FAULT\r\n");
    uart_protocol_set_busy(0U);
    board_set_boost(false);
#else
    send_control_disabled();
#endif
  } else if (strcmp(command, "TEST Y") == 0) {
#if GIMBAL_CONTROL_ENABLE
    board_set_boost(true);
    uart_protocol_send("TEST,Y_START\r\n");
    uart_protocol_set_busy(1U);
    uart_protocol_send(motor_pwm_test(GIMBAL_AXIS_Y) ? "TEST,Y_END\r\n" : "ERR,Y_FAULT\r\n");
    uart_protocol_set_busy(0U);
    board_set_boost(false);
#else
    send_control_disabled();
#endif
  } else if ((strncmp(command, "POS X ", 6U) == 0) ||
             (strncmp(command, "POS Y ", 6U) == 0)) {
    uart_protocol_set_busy(1U);
    execute_position_command(command);
    uart_protocol_set_busy(0U);
  } else if ((strncmp(command, "DEG X ", 6U) == 0) ||
             (strncmp(command, "DEG Y ", 6U) == 0)) {
    uart_protocol_set_busy(1U);
    execute_degree_command(command);
    uart_protocol_set_busy(0U);
  } else if (strncmp(command, "SET XY ", 7U) == 0) {
    uart_protocol_set_busy(1U);
    execute_xy_command(command, false);
    uart_protocol_set_busy(0U);
  } else if (strncmp(command, "MOVE XY ", 8U) == 0) {
    uart_protocol_set_busy(1U);
    execute_xy_command(command, true);
    uart_protocol_set_busy(0U);
  } else if (command[0] != '\0') {
    uart_protocol_send("ERR,UNKNOWN_CMD\r\n");
  }
}

void uart_protocol_init(void)
{
  rx_length = 0U;
  rx_discard_line = false;
  deferred_rx_read = 0U;
  deferred_rx_write = 0U;
  uart_error_count = 0U;
  protocol_busy = 0U;
  (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != UART4) return;
  uart_protocol_queue_rx_byte(uart_rx_irq_byte);
  (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != UART4) return;
  ++uart_error_count;
  huart->ErrorCode = HAL_UART_ERROR_NONE;
  (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
}

void uart_protocol_queue_rx_byte(uint8_t byte)
{
  uint8_t next = (uint8_t)((deferred_rx_write + 1U) % DEFERRED_RX_SIZE);
  if (next == deferred_rx_read) return;
  deferred_rx[deferred_rx_write] = byte;
  deferred_rx_write = next;
}

static void process_rx_byte(uint8_t byte)
{
  if (byte == '!') {
    rx_length = 0U;
    rx_discard_line = false;
    enter_safe_state();
    uart_protocol_send("OK,STOP\r\n");
  } else if (byte == '\r' || byte == '\n') {
    if (rx_discard_line) {
      rx_discard_line = false;
      rx_length = 0U;
      return;
    }
    if (rx_length != 0U) {
      char command[RX_LINE_SIZE];
      rx_line[rx_length] = '\0';
      memcpy(command, rx_line, rx_length + 1U);
      rx_length = 0U;
      execute(command);
    }
  } else if (rx_length + 1U < RX_LINE_SIZE) {
    if (!rx_discard_line) rx_line[rx_length++] = (char)byte;
  } else {
    rx_length = 0U;
    rx_discard_line = true;
    uart_protocol_send("ERR,LINE_TOO_LONG\r\n");
  }
}

void uart_protocol_process(void)
{
  while (deferred_rx_read != deferred_rx_write) {
    uint8_t byte = deferred_rx[deferred_rx_read];
    deferred_rx_read = (uint8_t)((deferred_rx_read + 1U) % DEFERRED_RX_SIZE);
    process_rx_byte(byte);
  }

  if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_ORE) ||
      __HAL_UART_GET_FLAG(&huart4, UART_FLAG_FE) ||
      __HAL_UART_GET_FLAG(&huart4, UART_FLAG_NE)) {
    ++uart_error_count;
    __HAL_UART_CLEAR_OREFLAG(&huart4);
    __HAL_UART_CLEAR_FEFLAG(&huart4);
    __HAL_UART_CLEAR_NEFLAG(&huart4);
    huart4.ErrorCode = HAL_UART_ERROR_NONE;
    rx_length = 0U;
  }

}
