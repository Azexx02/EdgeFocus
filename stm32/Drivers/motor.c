#include "motor.h"

// PA6 is configured as GPIO Output in CubeMX
// Motor driver (AO3400A MOSFET) on PA6: HIGH = vibrate, LOW = stop

void motor_init(void)
{
    // GPIO is already initialized by CubeMX (MX_GPIO_Init)
    // Just ensure motor is off
    motor_off();
}

void motor_on(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
}

void motor_off(void)
{
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
}

void motor_vibrate(uint32_t duration_ms)
{
    motor_on();
    HAL_Delay(duration_ms);
    motor_off();
}
