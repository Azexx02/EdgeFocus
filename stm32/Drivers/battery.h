#ifndef BATTERY_H
#define BATTERY_H

#include "stm32g0xx_hal.h"

// 电池电压检测 (PA4, ADC1_IN4)
// 硬件: R10=1MΩ, R11=470kΩ 分压, C10=100nF 滤波
// VBAT = Vadc * (R10+R11) / R11

void battery_init(ADC_HandleTypeDef *hadc);
uint16_t battery_read_raw(void);
uint16_t battery_read_mv(void);

#endif
