#ifndef UART_PROTOCOL_H
#define UART_PROTOCOL_H

void uart_protocol_init(void);
/* 在主循环中高频调用，函数自身不会等待完整数据帧。 */
void uart_protocol_process(void);
void uart_protocol_send(const char *text);

#endif
