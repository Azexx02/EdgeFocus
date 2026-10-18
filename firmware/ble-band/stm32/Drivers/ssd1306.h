#ifndef SSD1306_H
#define SSD1306_H

#include "stm32g0xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  32

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t i2c_addr;  // 7-bit address (0x3C or 0x3D)
} ssd1306_config_t;

HAL_StatusTypeDef ssd1306_init(const ssd1306_config_t *config);
HAL_StatusTypeDef ssd1306_clear(void);
HAL_StatusTypeDef ssd1306_display_on(void);
HAL_StatusTypeDef ssd1306_display_off(void);
HAL_StatusTypeDef ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on);
HAL_StatusTypeDef ssd1306_draw_string(uint8_t page, uint8_t col, const char *str);
HAL_StatusTypeDef ssd1306_update(void);

// Protocol display helpers
void ssd1306_show_countdown(uint16_t minutes);
void ssd1306_show_countdown_seconds(uint32_t seconds);
void ssd1306_show_countdown_status(uint32_t seconds, const char *status, const char *battery);
void ssd1306_show_alert(uint8_t level);

#endif // SSD1306_H
