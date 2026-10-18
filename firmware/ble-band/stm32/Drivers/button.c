#include "button.h"
#include "main.h"

void button_init(void)
{
    /* GPIO is configured by CubeMX. */
}

uint8_t button_read(void)
{
    /* PB12 uses external pull-up: released = high, pressed = low. */
    return !HAL_GPIO_ReadPin(buttom_wake_GPIO_Port, buttom_wake_Pin);
}

uint8_t button_wait_press(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint8_t last = button_read();

    while ((uint32_t)(HAL_GetTick() - start) < timeout_ms) {
        uint8_t cur = button_read();
        if (cur && !last) {
            HAL_Delay(50U);
            while (button_read()) {
                HAL_Delay(10U);
            }
            return 1U;
        }
        last = cur;
        HAL_Delay(10U);
    }
    return 0U;
}
