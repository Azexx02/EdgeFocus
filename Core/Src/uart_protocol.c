#include "uart_protocol.h"
#include "as5600.h"
#include "board_config.h"
#include "motor_pwm.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_LINE_SIZE 64U
static char rx_line[RX_LINE_SIZE];
static uint8_t rx_length;

void uart_protocol_send(const char *text)
{
  HAL_UART_Transmit(&huart4, (const uint8_t *)text, (uint16_t)strlen(text), 100U);
}

static void execute(const char *command)
{
  /* 当前协议用于板级联调，所有命令均以 CR/LF 结束。 */
  char response[64];
  if (strcmp(command, "PING") == 0) {
    uart_protocol_send("OK,G431\r\n");
  } else if (strcmp(command, "ANGLE") == 0) {
    uint16_t x = as5600_x_read_raw();
    uint16_t y = as5600_y_read_raw();
    snprintf(response, sizeof(response), "STAT,X=%u,Y=%u,EN=%u\r\n",
             x, y, board_get_enable_input() ? 1U : 0U);
    uart_protocol_send(response);
  } else if (strcmp(command, "BOOST 1") == 0) {
    board_set_boost(true); uart_protocol_send("OK,BOOST_ON\r\n");
  } else if (strcmp(command, "BOOST 0") == 0) {
    board_set_boost(false); uart_protocol_send("OK,BOOST_OFF\r\n");
  } else if (strcmp(command, "MOTOR 1") == 0) {
    uart_protocol_send(motor_pwm_set_enabled(true) ? "OK,MOTOR_ON\r\n" : "ERR,MOTOR_START\r\n");
  } else if (strcmp(command, "MOTOR 0") == 0) {
    motor_pwm_set_enabled(false); uart_protocol_send("OK,MOTOR_OFF\r\n");
  } else if (strcmp(command, "TEST X") == 0) {
    uart_protocol_send("TEST,X_START\r\n");
    uart_protocol_send(motor_pwm_test(GIMBAL_AXIS_X) ? "TEST,X_END\r\n" : "ERR,X_FAULT\r\n");
  } else if (strcmp(command, "TEST Y") == 0) {
    uart_protocol_send("TEST,Y_START\r\n");
    uart_protocol_send(motor_pwm_test(GIMBAL_AXIS_Y) ? "TEST,Y_END\r\n" : "ERR,Y_FAULT\r\n");
  } else if ((strncmp(command, "POS X ", 6U) == 0) ||
             (strncmp(command, "POS Y ", 6U) == 0)) {
    gimbal_axis_t axis = command[4] == 'X' ? GIMBAL_AXIS_X : GIMBAL_AXIS_Y;
    char axis_name = command[4];
    char *end;
    unsigned long target = strtoul(command + 6, &end, 10);
    if (*end != '\0' || target > 4095UL) {
      uart_protocol_send("ERR,BAD_POSITION\r\n");
    } else {
      uint16_t final_raw = AS5600_READ_ERROR;
      board_set_boost(true);
      snprintf(response, sizeof(response), "POS,%c_START,TARGET=%lu\r\n", axis_name, target);
      uart_protocol_send(response);
      bool ok = motor_pwm_set_position(axis, (uint16_t)target, &final_raw);
      board_set_boost(false);
      if (ok) {
        snprintf(response, sizeof(response), "POS,%c=%u,TARGET=%lu\r\n",
                 axis_name, final_raw, target);
      } else {
        snprintf(response, sizeof(response), "ERR,%c_POS,RAW=%u\r\n",
                 axis_name, final_raw);
      }
      uart_protocol_send(response);
    }
  } else if (command[0] != '\0') {
    uart_protocol_send("ERR,UNKNOWN_CMD\r\n");
  }
}

void uart_protocol_init(void) { rx_length = 0U; }

void uart_protocol_process(void)
{
  /* 非阻塞轮询接收，避免主循环被单个不完整命令卡住。 */
  uint8_t byte;
  while (HAL_UART_Receive(&huart4, &byte, 1U, 0U) == HAL_OK) {
    if (byte == '\r' || byte == '\n') {
      if (rx_length != 0U) {
        rx_line[rx_length] = '\0'; execute(rx_line); rx_length = 0U;
      }
    } else if (rx_length + 1U < RX_LINE_SIZE) {
      rx_line[rx_length++] = (char)byte;
    } else {
      rx_length = 0U; uart_protocol_send("ERR,LINE_TOO_LONG\r\n");
    }
  }
}
