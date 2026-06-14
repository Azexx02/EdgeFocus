#include "button.h"
#include "main.h"

void button_init(void)
{
    // GPIO 已由 CubeMX 配置 (PB12, EXTI rising)
    // 这里无需额外初始化
}

uint8_t button_read(void)
{
    // 返回 1=按下, 0=未按下
    return !HAL_GPIO_ReadPin(buttom_wake_GPIO_Port, buttom_wake_Pin);
}

uint8_t button_wait_press(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t last = button_read();

    while ((HAL_GetTick() - start) < timeout_ms) {
        uint8_t cur = button_read();
        // 下降沿: 之前松开(0), 现在按下(1)
        if (cur && !last) {
            // 等松手防抖
            HAL_Delay(50);
            while (button_read())
                HAL_Delay(10);
            return 1;
        }
        last = cur;
        HAL_Delay(10);
    }
    return 0;
}
