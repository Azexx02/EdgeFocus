#include "uart_protocol.h"

#include "app_gimbal.h"
#include "board_config.h"
#include "current_sample.h"
#include "gimbal_protocol.h"
#include "gimbal_rx.h"
#include "motor_pwm.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define DEFERRED_RX_SIZE 512U
#define UART_TX_BUFFER_SIZE 1024U

static GimbalRxFramer rx_framer;
static uint8_t deferred_rx[DEFERRED_RX_SIZE];
static volatile uint16_t deferred_rx_read;
static volatile uint16_t deferred_rx_write;
static volatile bool emergency_pending;
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
static bool hello_complete;

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
  length = write > read ? (uint16_t)(write - read) :
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
  uint32_t primask;
  if (text == NULL) return;
  primask = __get_PRIMASK();
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

void uart_protocol_set_busy(uint8_t busy)
{
  protocol_busy = busy != 0U ? 1U : 0U;
}

uint8_t uart_protocol_is_busy(void)
{
  return protocol_busy;
}

static void send_error(const char *class_name, const char *code)
{
  char response[96];
  snprintf(response, sizeof(response), "ERR,CLASS=%s,CODE=%s\r\n",
           class_name, code);
  uart_protocol_send(response);
}

static void send_app_error(AppGimbalResult result)
{
  const char *class_name = "COMMAND";
  if (result == APP_GIMBAL_BUSY || result == APP_GIMBAL_SAMPLE_STALE) {
    class_name = "TRANSIENT";
  } else if (result == APP_GIMBAL_DRIVER_FAULT ||
             result == APP_GIMBAL_NVM_FAILED) {
    class_name = "FAULT";
  }
  send_error(class_name, app_gimbal_result_code(result));
}

static const char *slot_status_name(GimbalNvSlotStatus status)
{
  switch (status) {
  case GIMBAL_NV_SLOT_EMPTY: return "EMPTY";
  case GIMBAL_NV_SLOT_VALID: return "VALID";
  case GIMBAL_NV_SLOT_UNSUPPORTED_SCHEMA: return "UNSUPPORTED_SCHEMA";
  case GIMBAL_NV_SLOT_CORRUPT: return "CORRUPT";
  default: return "UNKNOWN";
  }
}

static void format_value(char output[8], bool valid, uint16_t value)
{
  if (valid) snprintf(output, 8U, "%u", value);
  else memcpy(output, "NA", 3U);
}

static void send_pose(void)
{
  AppGimbalSnapshot pose;
  char yaw[8];
  char pitch[8];
  char target_yaw[8];
  char target_pitch[8];
  char response[512];
  CurrentSampleSnapshot current;
  app_gimbal_snapshot(&pose);
  current_sample_snapshot(&current);
  format_value(yaw, pose.logical_angles_valid, pose.yaw10);
  format_value(pitch, pose.logical_angles_valid, pose.pitch10);
  format_value(target_yaw, pose.target_valid, pose.target_yaw10);
  format_value(target_pitch, pose.target_valid, pose.target_pitch10);
  snprintf(response, sizeof(response),
           "POSE,STATE=%s,ARM_PROFILE=%s,P_RAW=%u,Y_RAW=%u,P_RAW_VALID=%u,Y_RAW_VALID=%u,YAW10=%s,PITCH10=%s,TYAW10=%s,TPITCH10=%s,CURRENT_VALID=%u,TARGET_VALID=%u,GEN=%lu,SETTLED=%u,CAL_EPOCH=%lu,ORIGIN_EPOCH=%lu,CAL_VALID=%u,ORIGIN_VALID=%u,CURRENT_PROTECTION=%s,SAMPLE_AGE_MS=%lu,FAULT=%s,OP=%s,OP_GEN=%lu,OP_PROGRESS=%u,LAST_OP_STATUS=%s\r\n",
           gimbal_protocol_state_name(pose.state),
           app_gimbal_profile_name(pose.arm_profile), pose.p_raw, pose.y_raw,
           pose.p_raw_valid ? 1U : 0U, pose.y_raw_valid ? 1U : 0U,
           yaw, pitch, target_yaw, target_pitch,
           current.valid ? 1U : 0U,
           pose.target_valid ? 1U : 0U,
           (unsigned long)pose.motion_generation, pose.settled ? 1U : 0U,
           (unsigned long)pose.calibration_epoch,
           (unsigned long)pose.origin_epoch,
           (pose.valid_flags & GIMBAL_NV_VALID_ELECTRICAL) != 0U ? 1U : 0U,
           (pose.valid_flags & GIMBAL_NV_VALID_ORIGIN) != 0U ? 1U : 0U,
           (pose.valid_flags & GIMBAL_NV_VALID_CURRENT_PROTECTION) != 0U ?
               "CONFIGURED" : "UNCONFIGURED",
           (unsigned long)pose.sample_age_ms, pose.fault_code,
           pose.operation, (unsigned long)pose.operation_generation,
           pose.operation_progress, pose.last_operation_status);
  uart_protocol_send(response);
}

static void send_current(void)
{
  CurrentSampleSnapshot current;
  char response[192];
  current_sample_snapshot(&current);
  snprintf(response, sizeof(response),
           "CURRENT,P_RAW=%u,P_AVG=%u,P_PEAK=%u,Y_RAW=%u,Y_AVG=%u,Y_PEAK=%u,WINDOW_MS=%lu,SAMPLES=%lu,VALID=%u,SATURATED=%u,SAMPLE_AGE_MS=%lu\r\n",
           current.raw[0], current.average[0], current.peak[0],
           current.raw[1], current.average[1], current.peak[1],
           (unsigned long)current.window_ms,
           (unsigned long)current.samples_since_reset,
           current.valid ? 1U : 0U, current.saturated ? 1U : 0U,
           (unsigned long)current.sample_age_ms);
  uart_protocol_send(response);
}

static void send_stop_response(void)
{
  char response[96];
  snprintf(response, sizeof(response),
           "OK,STOP,STATE=%s,BOOST=%u,MOTOR=%u,HOLD=%u\r\n",
           gimbal_protocol_state_name(app_gimbal_state()),
           board_get_boost() ? 1U : 0U, motor_pwm_is_enabled() ? 1U : 0U,
           motor_pwm_hold_is_enabled() ? 1U : 0U);
  uart_protocol_send(response);
}

static void execute(const char *line, size_t length)
{
  GimbalProtocolCommand command;
  GimbalParseResult parsed = gimbal_protocol_parse(line, length, &command);
  AppGimbalResult result;
  char response[256];
  uint32_t generation;
  if (parsed != GIMBAL_PARSE_OK) {
    if (parsed == GIMBAL_PARSE_UNKNOWN_COMMAND) send_error("COMMAND", "UNKNOWN_CMD");
    else if (parsed == GIMBAL_PARSE_BAD_RANGE) send_error("COMMAND", "BAD_RANGE");
    else send_error("COMMAND", "BAD_FORMAT");
    return;
  }
  if (protocol_busy && command.type != GIMBAL_CMD_STOP &&
      command.type != GIMBAL_CMD_PING && command.type != GIMBAL_CMD_VER &&
      command.type != GIMBAL_CMD_POSE && command.type != GIMBAL_CMD_FAULT) {
    send_error("TRANSIENT", "BUSY");
    return;
  }
  if (command.type != GIMBAL_CMD_HELLO && command.type != GIMBAL_CMD_STOP &&
      gimbal_protocol_changes_state(command.type) && !hello_complete) {
    send_error("COMMAND", "NEGOTIATION_REQUIRED");
    return;
  }
  if (command.type != GIMBAL_CMD_STOP &&
      !gimbal_protocol_command_allowed(app_gimbal_state(), command.type)) {
    send_error("COMMAND", "INVALID_STATE");
    return;
  }
  switch (command.type) {
  case GIMBAL_CMD_PING:
    uart_protocol_send("OK,G431\r\n");
    break;
  case GIMBAL_CMD_VER:
    uart_protocol_send("VER,PROTO=3.0,FW=G431-GIMBAL,CAPS=ASYNC_MOTION|POSE|ORIGIN|FAULT_CLASS|HOLD_NOW|NVM_V3|NEGOTIATION_REQUIRED,MAX_SPEED10=NA,MAX_ACCEL10=NA,SETTLE_MS=250\r\n");
    break;
  case GIMBAL_CMD_HELLO:
    hello_complete = true;
    uart_protocol_send("OK,HELLO,PROTO=3.0\r\n");
    break;
  case GIMBAL_CMD_ARM:
  case GIMBAL_CMD_ARM_MAINTENANCE:
    result = app_gimbal_arm_profile(command.type == GIMBAL_CMD_ARM ?
        APP_GIMBAL_PROFILE_PRODUCTION : APP_GIMBAL_PROFILE_MAINTENANCE);
    if (result != APP_GIMBAL_OK) { send_app_error(result); break; }
    snprintf(response, sizeof(response),
             "OK,ARM,STATE=READY,CAL_EPOCH=%lu,ORIGIN_EPOCH=%lu\r\n",
             (unsigned long)app_gimbal_nv_storage.record.calibration_epoch,
             (unsigned long)app_gimbal_nv_storage.record.origin_epoch);
    uart_protocol_send(response);
    break;
  case GIMBAL_CMD_STOP:
    (void)app_gimbal_stop();
    send_stop_response();
    break;
  case GIMBAL_CMD_HOLD_NOW:
    result = app_gimbal_hold_now(&generation);
    if (result != APP_GIMBAL_OK) { send_app_error(result); break; }
    {
      AppGimbalSnapshot pose;
      app_gimbal_snapshot(&pose);
      snprintf(response, sizeof(response),
               "OK,HOLD_NOW,GEN=%lu,YAW10=%u,PITCH10=%u\r\n",
               (unsigned long)generation, pose.yaw10, pose.pitch10);
      uart_protocol_send(response);
    }
    break;
  case GIMBAL_CMD_TRACK_YP:
    result = app_gimbal_track(command.argument.track.yaw10,
                              command.argument.track.pitch10, &generation);
    if (result != APP_GIMBAL_OK) { send_app_error(result); break; }
    snprintf(response, sizeof(response),
             "TRACK,ACCEPTED,GEN=%lu,YAW10=%u,PITCH10=%u\r\n",
             (unsigned long)generation, command.argument.track.yaw10,
             command.argument.track.pitch10);
    uart_protocol_send(response);
    break;
  case GIMBAL_CMD_POSE:
    send_pose();
    break;
  case GIMBAL_CMD_FAULT:
    snprintf(response, sizeof(response),
             "FAULT,STATE=%s,ACTIVE=%u,BOOST=%u,MOTOR=%u,HOLD=%u,UARTERR=%lu,RXDROP=%lu,TXDROP=%lu\r\n",
             gimbal_protocol_state_name(app_gimbal_state()),
             board_get_fault() ? 1U : 0U, board_get_boost() ? 1U : 0U,
             motor_pwm_is_enabled() ? 1U : 0U,
             motor_pwm_hold_is_enabled() ? 1U : 0U,
             (unsigned long)uart_error_count,
             (unsigned long)uart_rx_drop_count,
             (unsigned long)uart_tx_drop_count);
    uart_protocol_send(response);
    break;
  case GIMBAL_CMD_FAULT_CLEAR:
    result = app_gimbal_fault_clear();
    if (result != APP_GIMBAL_OK) send_app_error(result);
    else uart_protocol_send("OK,FAULT_CLEAR,STATE=DISABLED\r\n");
    break;
  case GIMBAL_CMD_ORIGIN_CAPTURE:
    result = app_gimbal_origin_capture();
    if (result != APP_GIMBAL_OK) send_app_error(result);
    else {
      snprintf(response, sizeof(response), "OK,ORIGIN_CAPTURE,ORIGIN_EPOCH=%lu\r\n",
               (unsigned long)app_gimbal_nv_storage.record.origin_epoch);
      uart_protocol_send(response);
    }
    break;
  case GIMBAL_CMD_NVM_STATUS:
    snprintf(response, sizeof(response),
             "NVM,SCHEMA=3,ACTIVE_SLOT=%d,GEN=%lu,A=%s,B=%s,FLAGS=%lu,CAL_EPOCH=%lu,ORIGIN_EPOCH=%lu\r\n",
             (int)app_gimbal_nv_storage.active_slot,
             (unsigned long)app_gimbal_nv_storage.record.generation,
             slot_status_name(app_gimbal_nv_storage.slot_status[0]),
             slot_status_name(app_gimbal_nv_storage.slot_status[1]),
             (unsigned long)app_gimbal_nv_storage.record.valid_flags,
             (unsigned long)app_gimbal_nv_storage.record.calibration_epoch,
             (unsigned long)app_gimbal_nv_storage.record.origin_epoch);
    uart_protocol_send(response);
    break;
  case GIMBAL_CMD_NVM_CLEAR_ORIGIN:
    result = app_gimbal_nvm_clear_origin();
    if (result != APP_GIMBAL_OK) send_app_error(result);
    else uart_protocol_send("OK,NVM_CLEAR_ORIGIN\r\n");
    break;
  case GIMBAL_CMD_NVM_CLEAR_ALL:
    result = app_gimbal_nvm_clear_all();
    if (result != APP_GIMBAL_OK) send_app_error(result);
    else uart_protocol_send("OK,NVM_CLEAR_ALL\r\n");
    break;
  case GIMBAL_CMD_CURRENT:
    send_current();
    break;
  case GIMBAL_CMD_CURRENT_RESET:
    current_sample_reset_statistics();
    uart_protocol_send("OK,CURRENT_RESET\r\n");
    break;
  case GIMBAL_CMD_PROTECT_CURRENT_SET:
    {
      GimbalCurrentProtectionConfig config;
      config.peak_limit_raw[0] = command.argument.protection.p_peak;
      config.average_limit_raw[0] = command.argument.protection.p_average;
      config.peak_limit_raw[1] = command.argument.protection.y_peak;
      config.average_limit_raw[1] = command.argument.protection.y_average;
      config.stall_ms = command.argument.protection.stall_ms;
      result = app_gimbal_set_current_protection(&config);
      if (result != APP_GIMBAL_OK) send_app_error(result);
      else uart_protocol_send("OK,PROTECT_CURRENT_SET\r\n");
    }
    break;
  case GIMBAL_CMD_PROTECT_CURRENT_CLEAR:
    result = app_gimbal_clear_current_protection();
    if (result != APP_GIMBAL_OK) send_app_error(result);
    else uart_protocol_send("OK,PROTECT_CURRENT_CLEAR\r\n");
    break;
  case GIMBAL_CMD_CAL_ELECTRICAL:
    result = app_gimbal_calibration_start(
        command.argument.calibration.axis, &generation);
    if (result != APP_GIMBAL_OK) send_app_error(result);
    else {
      const char *axis_name = command.argument.calibration.axis ==
          GIMBAL_PROTOCOL_AXIS_P ? "P" :
          (command.argument.calibration.axis == GIMBAL_PROTOCOL_AXIS_Y ?
           "Y" : "ALL");
      snprintf(response, sizeof(response),
               "CAL,ACCEPTED,OP_GEN=%lu,AXIS=%s\r\n",
               (unsigned long)generation, axis_name);
      uart_protocol_send(response);
    }
    break;
  default:
    send_error("COMMAND", "UNKNOWN_CMD");
    break;
  }
}

static void perform_emergency_stop(void)
{
  emergency_pending = false;
  deferred_rx_read = deferred_rx_write;
  gimbal_rx_init(&rx_framer);
  hello_complete = false;
  app_gimbal_emergency_stop();
  send_stop_response();
}

void uart_protocol_init(void)
{
  gimbal_rx_init(&rx_framer);
  deferred_rx_read = 0U;
  deferred_rx_write = 0U;
  emergency_pending = false;
  uart_error_count = 0U;
  uart_rx_drop_count = 0U;
  uart_tx_drop_count = 0U;
  uart_tx_read = 0U;
  uart_tx_write = 0U;
  uart_tx_active_length = 0U;
  uart_tx_active = false;
  uart_tx_start_tick = 0U;
  protocol_busy = 0U;
  hello_complete = false;
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
  gimbal_rx_init(&rx_framer);
  hello_complete = false;
  (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
}

void uart_protocol_queue_rx_byte(uint8_t byte)
{
  uint16_t next;
  if (byte == '!') {
    emergency_pending = true;
    return;
  }
  next = (uint16_t)((deferred_rx_write + 1U) % DEFERRED_RX_SIZE);
  if (next == deferred_rx_read) {
    ++uart_rx_drop_count;
    return;
  }
  deferred_rx[deferred_rx_write] = byte;
  deferred_rx_write = next;
}

static void process_rx_byte(uint8_t byte)
{
  const char *line;
  size_t length;
  GimbalRxEvent event = gimbal_rx_feed(&rx_framer, byte, &line, &length);
  if (event == GIMBAL_RX_LINE) {
    execute(line, length);
  } else if (event == GIMBAL_RX_BAD_FORMAT) {
    send_error("COMMAND", "BAD_FORMAT");
  } else if (event == GIMBAL_RX_EMERGENCY) {
    emergency_pending = true;
  }
}

void uart_protocol_process(void)
{
  if (emergency_pending) perform_emergency_stop();
  if (uart_tx_active &&
      (uint32_t)(HAL_GetTick() - uart_tx_start_tick) > 100U) {
    (void)HAL_UART_AbortTransmit(&huart4);
    uart_tx_active = false;
    uart_tx_active_length = 0U;
    uart_tx_start_tick = 0U;
    ++uart_error_count;
  }
  uart_tx_start();
  while (deferred_rx_read != deferred_rx_write) {
    uint8_t byte;
    if (emergency_pending) {
      perform_emergency_stop();
      break;
    }
    byte = deferred_rx[deferred_rx_read];
    deferred_rx_read = (uint16_t)((deferred_rx_read + 1U) % DEFERRED_RX_SIZE);
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
    huart4.RxState = HAL_UART_STATE_READY;
    gimbal_rx_init(&rx_framer);
    hello_complete = false;
    (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
  } else if (huart4.RxState == HAL_UART_STATE_READY) {
    (void)HAL_UART_Receive_IT(&huart4, &uart_rx_irq_byte, 1U);
  }
}
