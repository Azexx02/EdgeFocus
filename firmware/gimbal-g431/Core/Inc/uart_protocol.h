#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

#include <stdint.h>

void uart_protocol_init(void);
/* 在主循环中高频调用，函数自身不会等待完整数据帧。 */
void uart_protocol_process(void);
void uart_protocol_send(const char *text);
void uart_protocol_queue_rx_byte(uint8_t byte);
void uart_protocol_set_busy(uint8_t busy);
uint8_t uart_protocol_is_busy(void);

#endif
