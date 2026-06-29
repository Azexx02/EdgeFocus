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


typedef enum {
    SSD1306_CN_CONNECTED,
    SSD1306_CN_ADVERTISING,
    SSD1306_CN_OFFLINE,
    SSD1306_CN_LINKING_HOST,
    SSD1306_CN_PLEASE_WAIT,
    SSD1306_CN_UART_TIMEOUT,
    SSD1306_CN_BAD_REPLY,
    SSD1306_CN_SETUP_FAILED,
    SSD1306_CN_REST,
    SSD1306_CN_FOCUS_WARNING,
    SSD1306_CN_ATTENTION,
    SSD1306_CN_GENTLE,
    SSD1306_CN_ALERT,
    SSD1306_CN_MOTOR_ACTIVE,
    SSD1306_CN_CHARGING,
    SSD1306_CN_SLEEPING,
    SSD1306_CN_WAKING,
    SSD1306_CN_BLE_RESUME,
    SSD1306_CN_BLE_ERROR,
    SSD1306_CN_CONFIG_FAILED,
    SSD1306_CN_CHECK_SCREEN,
    SSD1306_CN_MANUAL_SETUP,
    SSD1306_CN_RESTARTING,
    SSD1306_CN_BLE_CONFIG,
    SSD1306_CN_COUNTING,
    SSD1306_CN_PHONE_ALERT,
    SSD1306_CN_STRONG_ALERT,
    SSD1306_CN_UNKNOWN_ALERT,
    SSD1306_CN_PHRASE_COUNT
} ssd1306_cn_phrase_t;

HAL_StatusTypeDef ssd1306_draw_cn_phrase(uint8_t page, uint8_t col, ssd1306_cn_phrase_t phrase);
HAL_StatusTypeDef ssd1306_draw_cn_phrase_center(uint8_t page, ssd1306_cn_phrase_t phrase);
void ssd1306_show_countdown_status_cn(uint32_t seconds, ssd1306_cn_phrase_t status);

#endif // SSD1306_H
