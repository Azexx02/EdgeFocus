#ifndef MAIN_APP_H
#define MAIN_APP_H

#include <stdbool.h>

// BLE callbacks - to be called from USART1 ISR or BLE task
void ble_on_signal_received(void);
void ble_on_connected(void);
void ble_on_disconnected(void);

// Application entry (call from CubeMX main.c after MX_xxx_Init())
void main_app(void);

#endif // MAIN_APP_H
