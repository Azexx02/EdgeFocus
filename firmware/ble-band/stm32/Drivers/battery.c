#include "battery.h"

#define VOLTAGE_DIVIDER_NUM  1470
#define VOLTAGE_DIVIDER_DEN  470
#define ADC_VREF_MV          2958

static ADC_HandleTypeDef *hadc_battery;

void battery_init(ADC_HandleTypeDef *hadc)
{
    hadc_battery = hadc;
}

uint16_t battery_read_raw(void)
{
    HAL_ADC_Start(hadc_battery);
    HAL_ADC_PollForConversion(hadc_battery, 100);
    uint16_t val = HAL_ADC_GetValue(hadc_battery);
    HAL_ADC_Stop(hadc_battery);
    return val;
}

uint16_t battery_read_mv(void)
{
    uint16_t raw = battery_read_raw();
    uint32_t mv_pin = (uint32_t)raw * ADC_VREF_MV / 4095;
    uint32_t mv_bat = mv_pin * VOLTAGE_DIVIDER_NUM / VOLTAGE_DIVIDER_DEN;
    return (uint16_t)mv_bat;
}

uint8_t battery_percent_from_mv(uint16_t mv)
{
    if (mv >= 4200U) {
        return 100U;
    }
    if (mv <= 3300U) {
        return 0U;
    }
    return (uint8_t)(((uint32_t)(mv - 3300U) * 100U) / 900U);
}

uint8_t battery_read_percent(void)
{
    return battery_percent_from_mv(battery_read_mv());
}
