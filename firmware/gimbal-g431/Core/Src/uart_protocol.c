#include "uart_protocol.h"
#include "app_gimbal.h"
#include "board_config.h"
#include "current_sample.h"
#include "gimbal_config.h"
#include "motor_pwm.h"
#include "usart.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_LINE_SIZE 64U
#define DEFERRED_RX_SIZE 128U
#define UART_TX_BUFFER_SIZE 512U
#define UART_RX_IDLE_COMMIT_MS 30U
#define PROTOCOL_VERSION "1.0"

static char rx_line[RX_LINE_SIZE];
static uint8_t rx_length;
static bool rx_discard_line;
static uint8_t deferred_rx[DEFERRED_RX_SIZE];
static volatile uint8_t deferred_rx_read;
static volatile uint8_t deferred_rx_write;
static volatile uint32_t uart_rx_last_byte_tick;
static uint32_t uart_error_count;
static uint32_t uart_rx_drop_count;
static uint32_t uart_tx_drop_count;
static uint8_t uart_tx_buffer[UART_TX_BUFFER_SIZE];
static volatile uint16_t uart_tx_read;
static volatile uint16_t uart_tx_write;
static volatile uint16_t uart_tx_active_length;
static volatile bool uart_tx_active;
static volatile uint32_t uart_tx_start_tick;
static volatile uint8_t protocol_busy;
static uint8_t uart_rx_irq_byte;
static bool circle_active;
static uint16_t circle_center_p;
static uint16_t circle_center_y;
static uint16_t circle_start_p;
static uint16_t circle_start_y;
static uint16_t circle_radius_deg;
static uint32_t circle_period_ms;
static uint32_t circle_start_tick;
static uint32_t circle_next_tick;

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
         strcmp(command, "FOCSTAT") == 0;
}

static bool command_requires_arm(const char *command)
{
  return strcmp(command, "HOLD 1") == 0 ||
         strncmp(command, "PITCH ", 6U) == 0 ||
         strncmp(command, "YAW ", 4U) == 0 ||
         strncmp(command, "DEG ", 4U) == 0 ||
         strncmp(command, "SET ", 4U) == 0 ||
         strncmp(command, "TRACK ", 6U) == 0 ||
         strncmp(command, "CIRCLE ", 7U) == 0;
}

static void uart_tx_start(void)
{
  uint32_t primask;
  uint16_t read;
  uint16_t write;
  uint16_t length;

  primask = __get_PRIMASK();
  __disable_irq();
  if (uart_tx_active || uart_tx_read == uart_tx_write) {
    __set_PRIMASK(primask);
    return;
  }
  read = uart_tx_read;
  write = uart_tx_write;
  length = (write > read) ? (uint16_t)(write - read) :
                            (uint16_t)(UART_TX_BUFFER_SIZE - read);
  uart_tx_active = true;
  uart_tx_active_length = length;
  uart_tx_start_tick = HAL_GetTick();
  __set_PRIMASK(primask);

  if (HAL_UART_Transmit_IT(&huart4, &uart_tx_buffer[read], length) != HAL_OK) {
    primask = __get_PRIMASK();
    __disable_irq();
    uart_tx_active = false;
    uart_tx_active_length = 0U;
    ++uart_error_count;
    __set_PRIMASK(primask);
  }
}

void uart_protocol_send(const char *text)
{
  bool overflow = false;
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  while (*text != '\0') {
    uint16_t next = (uint16_t)((uart_tx_write + 1U) % UART_TX_BUFFER_SIZE);
    if (next == uart_tx_read) {
      overflow = true;
      break;
    }
    uart_tx_buffer[uart_tx_write] = (uint8_t)*text++;
    uart_tx_write = next;
  }
  if (overflow) ++uart_tx_drop_count;
  __set_PRIMASK(primask);
  uart_tx_start();
}

#if !GIMBAL_CONTROL_ENABLE
static void send_control_disabled(void)
{
  uart_protocol_send("ERR,CONTROL_DISABLED\r\n");
}
#endif

static bool parse_axis(char c, gimbal_axis_t *axis)
{
  if (c == 'P') {
    *axis = GIMBAL_AXIS_P;
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
  circle_active = false;
  app_gimbal_disarm();
  board_set_dac_mv(GIMBAL_IDLE_DAC_MV, GIMBAL_IDLE_DAC_MV);
  board_set_fault(false);
}

static void circle_process(void)
{
  if (!circle_active) return;
  uint32_t now = HAL_GetTick();
  if ((int32_t)(now - circle_next_tick) < 0) return;
  circle_next_tick = now + 20U;

  uint32_t elapsed = now - circle_start_tick;
  if (elapsed >= circle_period_ms) {
    (void)motor_pwm_hold_set_target(GIMBAL_AXIS_P, circle_start_p);
    (void)motor_pwm_hold_set_target(GIMBAL_AXIS_Y, circle_start_y);
    circle_active = false;
    uart_protocol_send("CIRCLE,OK\r\n");
    return;
  }

  float phase = 6.28318530718f * (float)elapsed / (float)circle_period_ms;
  int32_t pitch = (int32_t)circle_center_p +
              (int32_t)lroundf((float)circle_radius_deg * cosf(phase));
  int32_t yaw = (int32_t)circle_center_y +
              (int32_t)lroundf((float)circle_radius_deg * sinf(phase));
  while (pitch < 0) pitch += 360;
  while (yaw < 0) yaw += 360;
  pitch %= 360;
  yaw %= 360;
  (void)motor_pwm_hold_set_target(GIMBAL_AXIS_P, (uint16_t)pitch);
  (void)motor_pwm_hold_set_target(GIMBAL_AXIS_Y, (uint16_t)yaw);
}

static bool parse_yp(const char *text, uint16_t *yaw, uint16_t *pitch)
{
  unsigned int yaw_value;
  unsigned int pitch_value;
  char extra;
  if (sscanf(text, "%u %u %c", &yaw_value, &pitch_value, &extra) != 2 ||
      yaw_value > motor_pwm_get_limit_deg(GIMBAL_AXIS_Y) ||
      pitch_value > motor_pwm_get_limit_deg(GIMBAL_AXIS_P)) {
    return false;
  }
  *yaw = (uint16_t)yaw_value;
  *pitch = (uint16_t)pitch_value;
  return true;
}

static void execute_yp_command(const char *command)
{
#if GIMBAL_CONTROL_ENABLE
  char response[64];
  uint16_t target_yaw;
  uint16_t target_pitch;
  if (!parse_yp(command + 7U, &target_yaw, &target_pitch)) {
    uart_protocol_send("ERR,BAD_SET_YP\r\n");
    return;
  }
  if (!motor_pwm_calibration_is_valid(GIMBAL_AXIS_P) ||
      !motor_pwm_calibration_is_valid(GIMBAL_AXIS_Y)) {
    uart_protocol_send("ERR,NOT_HOMED\r\n");
    return;
  }

  if (!motor_pwm_hold_is_enabled()) motor_pwm_hold_enable(true);
  if (!motor_pwm_hold_is_enabled() ||
      !motor_pwm_hold_set_target(GIMBAL_AXIS_Y, target_yaw) ||
      !motor_pwm_hold_set_target(GIMBAL_AXIS_P, target_pitch)) {
    uart_protocol_send("ERR,HOLD_START\r\n");
    return;
  }
  snprintf(response, sizeof(response), "SET,YP_OK,Y=%u,P=%u\r\n",
           target_yaw, target_pitch);
  uart_protocol_send(response);
#else
  (void)command;
  send_control_disabled();
#endif
}

static void send_angle_response(void)
{
  char response[128];
  uint16_t p_raw = motor_pwm_read_raw(GIMBAL_AXIS_P);
  uint16_t y_raw = motor_pwm_read_raw(GIMBAL_AXIS_Y);
  uint16_t p_deg = UINT16_MAX;
  uint16_t y_deg = UINT16_MAX;

  (void)motor_pwm_raw_to_deg(GIMBAL_AXIS_P, p_raw, &p_deg);
  (void)motor_pwm_raw_to_deg(GIMBAL_AXIS_Y, y_raw, &y_deg);
  snprintf(response, sizeof(response),
           "ANGLE,P_RAW=%u,Y_RAW=%u,P_DEG=%u,Y_DEG=%u,P_HOME=%u,Y_HOME=%u,EN=%u\r\n",
           p_raw, y_raw, p_deg, y_deg,
           motor_pwm_get_home(GIMBAL_AXIS_P),
           motor_pwm_get_home(GIMBAL_AXIS_Y),
           board_get_enable_input() ? 1U : 0U);
  uart_protocol_send(response);
}

static void execute_degree_command(const char *command)
{
#if GIMBAL_CONTROL_ENABLE
  char response[80];
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
  if (!motor_pwm_deg_to_raw(axis, target_deg, &target_raw)) {
    uart_protocol_send("ERR,BAD_DEGREE\r\n");
    return;
  }

  if (!motor_pwm_hold_is_enabled()) {
    motor_pwm_hold_enable(true);
  }
  if (motor_pwm_hold_is_enabled() &&
      motor_pwm_hold_set_target(axis, target_deg)) {
    snprintf(response, sizeof(response),
             "DEG,%c_TARGET=%u,RAW=%u,TRACKING=1\r\n",
             command[4], target_deg, target_raw);
  } else {
    uart_protocol_send("ERR,HOLD_START\r\n");
    return;
  }
  uart_protocol_send(response);
#else
  (void)command;
  send_control_disabled();
#endif
}

static void execute(const char *command)
{
  char response[256];

  if (protocol_busy && !command_allowed_while_busy(command)) {
    uart_protocol_send("ERR,BUSY\r\n");
    return;
  }

#if GIMBAL_UART_ONLY_MODE
  if (!app_gimbal_is_armed() && command_requires_arm(command)) {
    uart_protocol_send("ERR,NOT_ARMED,USE_ARM\r\n");
    return;
  }
#endif

  if (strcmp(command, "PING") == 0) {
    uart_protocol_send("OK,G431\r\n");
  } else if (strcmp(command, "VER") == 0) {
    uart_protocol_send("VER,PROTO=2.0,FW=G431-GIMBAL,CAPS=UART_ONLY|ARM|YP_CONTINUOUS|FOC_HOLD|TRACK|CIRCLE\r\n");
  } else if (strcmp(command, "HELP") == 0) {
    uart_protocol_send("CMDS,PING,ARM,DISARM,STOP,PITCH,YAW,SET_YP,TRACK_YP,HOLD,ANGLE,STAT,HELP ALL\r\n");
  } else if (strcmp(command, "HELP ALL") == 0) {
    uart_protocol_send("CMDS,VER,PING,ARM,DISARM,STOP,SAFE,PITCH,YAW,DEG_P,DEG_Y,SET_YP,TRACK_YP,CIRCLE,HOLD,ANGLE,STAT,FAULT,FOCSTAT,GRAVCLEAR,ECHO\r\n");
  } else if (strncmp(command, "ECHO ", 5U) == 0) {
    uart_protocol_send("ECHO,");
    uart_protocol_send(command + 5);
    uart_protocol_send("\r\n");
  } else if (strcmp(command, "ARM") == 0) {
#if GIMBAL_UART_ONLY_MODE
    if (app_gimbal_arm()) {
      uart_protocol_send("OK,ARM\r\n");
    } else {
      uart_protocol_send("ERR,ARM\r\n");
    }
#else
    uart_protocol_send("ERR,USE_ENABLE_INPUT\r\n");
#endif
  } else if (strcmp(command, "DISARM") == 0) {
    enter_safe_state();
    uart_protocol_send("OK,DISARM\r\n");
  } else if (strcmp(command, "SAFE") == 0) {
    enter_safe_state();
    uart_protocol_send("OK,SAFE\r\n");
  } else if (strcmp(command, "STOP") == 0) {
    enter_safe_state();
    uart_protocol_send("OK,STOP\r\n");
  } else if (strcmp(command, "FAULT") == 0) {
    snprintf(response, sizeof(response),
             "FAULT,ACTIVE=%u,ENABLE_IN=%u,BOOT0=%u,I_P=%u,I_Y=%u,UARTERR=%lu,RXDROP=%lu,TXDROP=%lu\r\n",
             board_get_fault() ? 1U : 0U,
             board_get_enable_input() ? 1U : 0U,
             board_get_boot0_ctrl() ? 1U : 0U,
             current_get_x_raw(), current_get_y_raw(),
             (unsigned long)uart_error_count,
             (unsigned long)uart_rx_drop_count,
             (unsigned long)uart_tx_drop_count);
    uart_protocol_send(response);
  } else if (strcmp(command, "FOCSTAT") == 0) {
    gimbal_foc_status_t foc;
    motor_pwm_get_foc_status(&foc);
    snprintf(response, sizeof(response),
             "FOCSTAT,EN=%u,PALIGN=%u,YALIGN=%u,PE=%u,YE=%u,PDUTY=%u,YDUTY=%u,PGRAV=%d,PDIR=%d,YDIR=%d,POFF=%u,YOFF=%u,PPP=%u,YPP=%u,CYC=%lu,WD=%lu,UARTERR=%lu,RXDROP=%lu,TXDROP=%lu,PERIOD=%u\r\n",
             foc.enabled ? 1U : 0U, foc.x_aligned ? 1U : 0U,
             foc.y_aligned ? 1U : 0U, foc.x_error_raw, foc.y_error_raw,
             foc.x_duty_ticks, foc.y_duty_ticks, foc.x_gravity_comp,
             foc.x_direction, foc.y_direction,
             foc.x_electric_offset, foc.y_electric_offset,
             foc.x_pole_pairs, foc.y_pole_pairs,
             (unsigned long)foc.cycles, (unsigned long)foc.watchdog_trips,
             (unsigned long)uart_error_count,
             (unsigned long)uart_rx_drop_count,
             (unsigned long)uart_tx_drop_count, GIMBAL_FOC_PERIOD_MS);
    uart_protocol_send(response);
  } else if (strcmp(command, "GRAVCLEAR") == 0) {
    motor_pwm_gravity_clear();
    uart_protocol_send("OK,GRAVCLEAR\r\n");
  } else if (strcmp(command, "STAT") == 0) {
    snprintf(response, sizeof(response),
           "STAT,MODE=%s,SAFE=%u,BOOST=%u,MOTOR=%u,HOLD=%u,P_TARGET=%u,Y_TARGET=%u,EN=%u,BOOT0=%u,FAULT=%u,I_P=%u,I_Y=%u\r\n",
#if GIMBAL_UART_ONLY_MODE
             "UART_ONLY",
#else
             "INTERLOCK",
#endif
             GIMBAL_CONTROL_ENABLE ? 0U : 1U,
             board_get_boost() ? 1U : 0U,
             motor_pwm_is_enabled() ? 1U : 0U,
             motor_pwm_hold_is_enabled() ? 1U : 0U,
             motor_pwm_hold_get_target(GIMBAL_AXIS_P),
             motor_pwm_hold_get_target(GIMBAL_AXIS_Y),
             board_get_enable_input() ? 1U : 0U,
             board_get_boot0_ctrl() ? 1U : 0U,
             board_get_fault() ? 1U : 0U,
             current_get_x_raw(), current_get_y_raw());
    uart_protocol_send(response);
  } else if (strcmp(command, "ANGLE") == 0) {
    send_angle_response();
  } else if (strcmp(command, "HOLD 1") == 0) {
#if GIMBAL_CONTROL_ENABLE
#if GIMBAL_FOC_HOLD_ENABLE
    if (!motor_pwm_hold_is_enabled()) motor_pwm_hold_enable(true);
    if (motor_pwm_hold_is_enabled()) {
      snprintf(response, sizeof(response), "OK,HOLD_ON,P=%u,Y=%u\r\n",
               motor_pwm_hold_get_target(GIMBAL_AXIS_P),
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
  } else if (strncmp(command, "CIRCLE ", 7U) == 0) {
#if GIMBAL_CONTROL_ENABLE && GIMBAL_FOC_HOLD_ENABLE
    unsigned int radius;
    unsigned long period;
    char extra;
    if (sscanf(command + 7, "%u %lu %c", &radius, &period, &extra) != 2 ||
        radius < 1U || radius > 60U ||
        period < 1000UL || period > 60000UL) {
      uart_protocol_send("ERR,CIRCLE_FORMAT,CIRCLE radius_deg period_ms\r\n");
    } else {
      uint16_t p_raw = motor_pwm_read_raw(GIMBAL_AXIS_P);
      uint16_t y_raw = motor_pwm_read_raw(GIMBAL_AXIS_Y);
      uint16_t p_deg;
      uint16_t y_deg;
      if (!motor_pwm_raw_to_deg(GIMBAL_AXIS_P, p_raw, &p_deg) ||
          !motor_pwm_raw_to_deg(GIMBAL_AXIS_Y, y_raw, &y_deg)) {
        uart_protocol_send("ERR,CIRCLE_ANGLE\r\n");
      } else {
        motor_pwm_hold_enable(true);
        if (!motor_pwm_hold_is_enabled()) {
          uart_protocol_send("ERR,CIRCLE_START\r\n");
        } else {
          circle_start_p = p_deg;
          circle_start_y = y_deg;
          circle_center_p =
              (uint16_t)((p_deg + 360U - (uint16_t)radius) % 360U);
          circle_center_y = y_deg;
          circle_radius_deg = (uint16_t)radius;
          circle_period_ms = (uint32_t)period;
          circle_start_tick = HAL_GetTick();
          circle_next_tick = circle_start_tick;
          circle_active = true;
          uart_protocol_send("CIRCLE,START\r\n");
        }
      }
    }
#else
    uart_protocol_send("ERR,CIRCLE_UNAVAILABLE\r\n");
#endif
  } else if ((strncmp(command, "DEG Y ", 6U) == 0) ||
             (strncmp(command, "DEG P ", 6U) == 0)) {
    uart_protocol_set_busy(1U);
    execute_degree_command(command);
    uart_protocol_set_busy(0U);
  } else if (strncmp(command, "PITCH ", 6U) == 0) {
    char translated[RX_LINE_SIZE];
    snprintf(translated, sizeof(translated), "DEG P %s", command + 6);
    uart_protocol_set_busy(1U);
    execute_degree_command(translated);
    uart_protocol_set_busy(0U);
  } else if (strncmp(command, "YAW ", 4U) == 0) {
    char translated[RX_LINE_SIZE];
    snprintf(translated, sizeof(translated), "DEG Y %s", command + 4);
    uart_protocol_set_busy(1U);
    execute_degree_command(translated);
    uart_protocol_set_busy(0U);
  } else if (strncmp(command, "SET YP ", 7U) == 0) {
    uart_protocol_set_busy(1U);
    execute_yp_command(command);
    uart_protocol_set_busy(0U);
  } else if (strncmp(command, "TRACK YP ", 9U) == 0) {
    unsigned int yaw10;
    unsigned int pitch10;
    char extra;
    if (sscanf(command + 9, "%u %u %c", &yaw10, &pitch10, &extra) != 2 ||
        yaw10 > 3599U || pitch10 > 3599U) {
      uart_protocol_send("ERR,BAD_TRACK_YP\r\n");
    } else if (!motor_pwm_hold_is_enabled()) {
      uart_protocol_send("ERR,HOLD_REQUIRED\r\n");
    } else if (!motor_pwm_hold_set_target_tenths(
                   GIMBAL_AXIS_Y, (uint16_t)yaw10) ||
               !motor_pwm_hold_set_target_tenths(
                   GIMBAL_AXIS_P, (uint16_t)pitch10)) {
      uart_protocol_send("ERR,TRACK_TARGET\r\n");
    } else {
      uart_protocol_send("TRACK,YP_OK\r\n");
    }
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
  uart_rx_last_byte_tick = HAL_GetTick();
  uart_error_count = 0U;
  uart_rx_drop_count = 0U;
  uart_tx_drop_count = 0U;
  uart_tx_read = 0U;
  uart_tx_write = 0U;
  uart_tx_active_length = 0U;
  uart_tx_active = false;
  uart_tx_start_tick = 0U;
  protocol_busy = 0U;
  circle_active = false;
  (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != UART4) return;
  uart_protocol_queue_rx_byte(uart_rx_irq_byte);
  (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != UART4) return;
  uart_tx_read = (uint16_t)((uart_tx_read + uart_tx_active_length) %
                            UART_TX_BUFFER_SIZE);
  uart_tx_active_length = 0U;
  uart_tx_active = false;
  uart_tx_start_tick = 0U;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != UART4) return;
  ++uart_error_count;
  __HAL_UART_CLEAR_OREFLAG(huart);
  __HAL_UART_CLEAR_FEFLAG(huart);
  __HAL_UART_CLEAR_NEFLAG(huart);
  __HAL_UART_CLEAR_PEFLAG(huart);
  huart->ErrorCode = HAL_UART_ERROR_NONE;
  huart->RxState = HAL_UART_STATE_READY;
  (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
}

void uart_protocol_queue_rx_byte(uint8_t byte)
{
  uint8_t next = (uint8_t)((deferred_rx_write + 1U) % DEFERRED_RX_SIZE);
  if (next == deferred_rx_read) {
    ++uart_rx_drop_count;
    return;
  }
  deferred_rx[deferred_rx_write] = byte;
  deferred_rx_write = next;
  uart_rx_last_byte_tick = HAL_GetTick();
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
    if (!rx_discard_line) {
      /* Accept lower-case commands and comma-separated arguments. */
      if (byte == ',') byte = ' ';
      rx_line[rx_length++] = (char)toupper((int)byte);
    }
  } else {
    rx_length = 0U;
    rx_discard_line = true;
    uart_protocol_send("ERR,LINE_TOO_LONG\r\n");
  }
}

void uart_protocol_process(void)
{
  if (uart_tx_active &&
      (uint32_t)(HAL_GetTick() - uart_tx_start_tick) > 100U) {
    /*
     * Recover from a lost TC interrupt or a UART state-machine stall.
     * Keep uart_tx_read unchanged so the interrupted response is retried.
     */
    (void)HAL_UART_AbortTransmit(&huart4);
    uart_tx_active = false;
    uart_tx_active_length = 0U;
    uart_tx_start_tick = 0U;
    ++uart_error_count;
  }
  uart_tx_start();

  while (deferred_rx_read != deferred_rx_write) {
    uint8_t byte = deferred_rx[deferred_rx_read];
    deferred_rx_read = (uint8_t)((deferred_rx_read + 1U) % DEFERRED_RX_SIZE);
    process_rx_byte(byte);
  }

  /*
   * Many PC serial tools send exactly the visible text unless "append CR/LF"
   * is enabled.  Commit an idle line so commands such as PING still work.
   */
  if (rx_length != 0U && !rx_discard_line &&
      (uint32_t)(HAL_GetTick() - uart_rx_last_byte_tick) >=
          UART_RX_IDLE_COMMIT_MS) {
    process_rx_byte('\n');
  }

  if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_ORE) ||
      __HAL_UART_GET_FLAG(&huart4, UART_FLAG_FE) ||
      __HAL_UART_GET_FLAG(&huart4, UART_FLAG_NE)) {
    ++uart_error_count;
    __HAL_UART_CLEAR_OREFLAG(&huart4);
    __HAL_UART_CLEAR_FEFLAG(&huart4);
    __HAL_UART_CLEAR_NEFLAG(&huart4);
    huart4.ErrorCode = HAL_UART_ERROR_NONE;
    huart4.RxState = HAL_UART_STATE_READY;
    rx_length = 0U;
    (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
  } else if (huart4.RxState == HAL_UART_STATE_READY) {
    /* Recover if a transient HAL error left byte reception unarmed. */
    (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
  }

  circle_process();
}
