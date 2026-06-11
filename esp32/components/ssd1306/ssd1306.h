#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      32

typedef struct {
    int sda_pin;
    int scl_pin;
    uint8_t i2c_addr;  // 0x3C or 0x3D
} ssd1306_config_t;

esp_err_t ssd1306_init(const ssd1306_config_t *config);
esp_err_t ssd1306_clear(void);
esp_err_t ssd1306_display_on(void);
esp_err_t ssd1306_display_off(void);
esp_err_t ssd1306_draw_string(uint8_t page, uint8_t col, const char *str);
esp_err_t ssd1306_draw_pixel(uint8_t x, uint8_t y, bool on);
esp_err_t ssd1306_update(void);
