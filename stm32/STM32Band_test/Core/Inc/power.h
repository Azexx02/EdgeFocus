#ifndef POWER_H
#define POWER_H

#include "stm32g0xx_hal.h"

void power_init(void);
uint8_t power_ext_present(void);
uint8_t power_charging(void);

#endif
