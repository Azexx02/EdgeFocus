#ifndef MOTOR_H
#define MOTOR_H

#include "stm32g0xx_hal.h"

// Motor pin: PA6 (TIM16_CH1 PWM)
// PWM frequency = HSI(16MHz) / (Prescaler+1) / (Period+1) = 5kHz
// Duty cycle 0~100% controls vibration intensity

void motor_init(void);
void motor_set_duty(uint8_t percent);  // 0=off, 100=full speed
void motor_on(void);                   // 100% duty
void motor_off(void);                  // 0% duty
void motor_vibrate(uint32_t duration_ms); // blocking vibrate at 100%

#endif // MOTOR_H
