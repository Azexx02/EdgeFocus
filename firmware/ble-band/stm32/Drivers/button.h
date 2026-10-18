#ifndef BUTTON_H
#define BUTTON_H

#include "stm32g0xx_hal.h"

// 按键输入 (PB12, 外部上拉, 下降沿触发)
// 未按下=HIGH, 按下=LOW

void button_init(void);
uint8_t button_read(void);
uint8_t button_wait_press(uint32_t timeout_ms);

#endif
