#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define MOTOR_PIN  4
#define TAG        "motor_test"

void app_main(void)
{
    ESP_LOGI(TAG, "=== Motor Test on GPIO%d ===", MOTOR_PIN);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(MOTOR_PIN, 0);

    while (1) {
        ESP_LOGI(TAG, "Motor ON");
        gpio_set_level(MOTOR_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Motor OFF");
        gpio_set_level(MOTOR_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
