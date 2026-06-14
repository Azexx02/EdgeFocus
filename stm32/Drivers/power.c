#include "power.h"
#include "main.h"

void power_init(void)
{
    // GPIO 已由 CubeMX 配置
}

uint8_t power_ext_present(void)
{
    // PGOOD: LOW = 电源有效
    return !HAL_GPIO_ReadPin(External_power_validity_GPIO_Port, External_power_validity_Pin);
}

uint8_t power_charging(void)
{
    // CHG: LOW = 充电中
    return !HAL_GPIO_ReadPin(Charging_status_GPIO_Port, Charging_status_Pin);
}
