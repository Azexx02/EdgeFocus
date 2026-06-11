#ifndef MOTOR_H
#define MOTOR_H

#include "stm32g0xx_hal.h"

// Motor pin: PA6 (TIM16_CH1 or GPIO)
// Uses GPIO output by default (matches ESP32 behavior)
// PWM via TIM16_CH1 can be enabled later for intensity control

void motor_init(void);
void motor_vibrate(uint32_t duration_ms);
void motor_on(void);
void motor_off(void);

#endif // MOTOR_H
