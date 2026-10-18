#include "power.h"
#include "main.h"

void power_init(void)
{
    /* GPIO is configured by CubeMX. */
}

uint8_t power_ext_present(void)
{
    /* BQ24074 PGOOD is active low. */
    return !HAL_GPIO_ReadPin(External_power_validity_GPIO_Port, External_power_validity_Pin);
}

uint8_t power_charging(void)
{
    /* BQ24074 CHG is active low. */
    return !HAL_GPIO_ReadPin(Charging_status_GPIO_Port, Charging_status_Pin);
}
