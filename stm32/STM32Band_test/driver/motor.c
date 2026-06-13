#include "motor.h"

extern TIM_HandleTypeDef htim16;

#define MOTOR_TIM       htim16
#define MOTOR_TIM_CH    TIM_CHANNEL_1
#define MOTOR_TIM_ARR   3199

void motor_init(void)
{
    // ─── 强制重新配置，不依赖 CubeMX 初始化顺序 ───
    // 1. 使能 GPIOA 时钟（debug reset 后可能未开启）
    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;

    // 2. 配置 PA6 为 TIM16_CH1 (AF5)
    GPIOA->MODER   = (GPIOA->MODER   & ~(3 << 12)) | (2 << 12);  // AF mode
    GPIOA->AFR[0]  = (GPIOA->AFR[0]  & ~(0xF << 24)) | (5 << 24); // AF5
    GPIOA->OTYPER  &= ~(1 << 6);   // push-pull
    GPIOA->OSPEEDR &= ~(3 << 12);  // low speed
    GPIOA->PUPDR   &= ~(3 << 12);  // no pull

    // 3. 使能 TIM16 时钟
    RCC->APBENR2 |= RCC_APBENR2_TIM16EN;

    // 4. 重新配置定时器基本参数（CubeMX 的值）
    MOTOR_TIM.Instance->PSC   = 0;
    MOTOR_TIM.Instance->ARR   = MOTOR_TIM_ARR;
    MOTOR_TIM.Instance->RCR   = 0;
    MOTOR_TIM.Instance->CR1   = TIM_CR1_ARPE;
    MOTOR_TIM.Instance->CCMR1 = (6 << 4) | TIM_CCMR1_OC1PE; // PWM mode 1 + preload
    MOTOR_TIM.Instance->CCR1  = 0;

    // 5. 清除并重新使能输出
    MOTOR_TIM.Instance->CCER = 0;
    MOTOR_TIM.Instance->BDTR = 0;
    TIM_CCxChannelCmd(MOTOR_TIM.Instance, MOTOR_TIM_CH, TIM_CCx_ENABLE);
    __HAL_TIM_MOE_ENABLE(&MOTOR_TIM);

    // 6. 启动计数器
    __HAL_TIM_ENABLE(&MOTOR_TIM);

    motor_off();
}

void motor_set_duty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t pulse = (uint32_t)percent * (MOTOR_TIM_ARR + 1) / 100;
    __HAL_TIM_SET_COMPARE(&MOTOR_TIM, MOTOR_TIM_CH, pulse);
}

void motor_on(void)
{
    motor_set_duty(100);
}

void motor_off(void)
{
    __HAL_TIM_SET_COMPARE(&MOTOR_TIM, MOTOR_TIM_CH, 0);
}

void motor_vibrate(uint32_t duration_ms)
{
    motor_on();
    HAL_Delay(duration_ms);
    motor_off();
}
