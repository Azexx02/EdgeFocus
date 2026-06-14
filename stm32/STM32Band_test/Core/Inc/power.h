#ifndef POWER_H
#define POWER_H

#include "stm32g0xx_hal.h"

// 充电状态 + 外部电源检测
// PB1: BQ24074 CHG (充电状态, 低电平有效)
// PB11: BQ24074 PGOOD (外部电源有效, 低电平有效)

void power_init(void);
uint8_t power_ext_present(void);
uint8_t power_charging(void);

#endif
