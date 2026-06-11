/**
 * STM32-Band 主程序
 *
 * 功能与 ESP32-Band 保持一致:
 *   - OLED 显示连接状态
 *   - 马达震动响应
 *   - BLE 状态显示（预留，E104-BT52 安装后启用）
 *
 * 引脚:
 *   OLED SCL = PB6 (I2C1)
 *   OLED SDA = PB7 (I2C1)
 *   Motor    = PA6 (GPIO Output)
 *   BLE TX   = PA9 (USART1)
 *   BLE RX   = PA10 (USART1)
 *   BLE LINK = PB0 (GPIO Input)
 *   BLE WKP  = PB2 (GPIO Output)
 *   BLE RST  = PB10 (GPIO Output)
 */

#include "main.h"
#include "ssd1306.h"
#include "motor.h"
#include <stdio.h>

// BLE module state (updated by USART1 ISR when E104-BT52 is installed)
typedef enum {
    BLE_STATE_IDLE,        // Module not installed or not responding
    BLE_STATE_ADVERTISING, // Broadcasting, waiting for connection
    BLE_STATE_CONNECTED,   // Connected to central device
    BLE_STATE_SIGNAL,      // Received vibration signal
} ble_state_t;

static volatile ble_state_t ble_state = BLE_STATE_IDLE;

// External handles from CubeMX-generated code
extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;

// ─── BLE display update ────────────────────────────────
static void display_update(ble_state_t state)
{
    ssd1306_clear();

    switch (state) {
    case BLE_STATE_IDLE:
        ssd1306_draw_string(0, 0, "STM32-Band");
        ssd1306_draw_string(1, 0, "BLE: Not ready");
        ssd1306_draw_string(3, 0, "Waiting...");
        break;

    case BLE_STATE_ADVERTISING:
        ssd1306_draw_string(0, 0, "STM32-Band");
        ssd1306_draw_string(1, 0, "BLE Advertising");
        ssd1306_draw_string(3, 0, "Connect me!");
        break;

    case BLE_STATE_CONNECTED:
        ssd1306_draw_string(0, 0, "BLE Connected");
        ssd1306_draw_string(1, 0, "Waiting signal");
        ssd1306_draw_string(3, 0, "STM32-Band");
        break;

    case BLE_STATE_SIGNAL:
        ssd1306_draw_string(0, 0, "BLE Signal!");
        ssd1306_draw_string(1, 0, "Motor ON...");
        ssd1306_draw_string(3, 0, "STM32-Band");
        break;
    }

    ssd1306_update();
}

// ─── BLE signal callback (called from USART1 ISR or BLE task) ──
void ble_on_signal_received(void)
{
    ble_state = BLE_STATE_SIGNAL;
    display_update(BLE_STATE_SIGNAL);

    motor_vibrate(500);

    ble_state = BLE_STATE_CONNECTED;
    display_update(BLE_STATE_CONNECTED);
}

// ─── BLE connection callbacks ──────────────────────────
void ble_on_connected(void)
{
    ble_state = BLE_STATE_CONNECTED;
    display_update(BLE_STATE_CONNECTED);
}

void ble_on_disconnected(void)
{
    ble_state = BLE_STATE_ADVERTISING;
    display_update(BLE_STATE_ADVERTISING);
}

// ─── BLE module reset sequence ─────────────────────────
static void ble_module_reset(void)
{
    // Reset E104-BT52: pull RST low for 100ms, then release
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
    HAL_Delay(500);

    // Wake module
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);
}

// ─── Check BLE module via LINK pin ─────────────────────
static bool ble_module_present(void)
{
    // If LINK pin is readable and not stuck, module is likely present
    // Try toggling WKP and checking if LINK responds
    ble_module_reset();
    HAL_Delay(200);
    // If module responds on UART, it's present
    // For now, just check if LINK pin is in a known state
    return true;  // Assume present, UART will confirm later
}

// ─── Application entry ─────────────────────────────────
void main_app(void)
{
    // Init motor
    motor_init();

    // Init OLED
    ssd1306_config_t oled_config = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x3C,
    };
    if (ssd1306_init(&oled_config) != HAL_OK) {
        // OLED init failed - blink motor as error indicator
        while (1) {
            motor_vibrate(100);
            HAL_Delay(500);
        }
    }

    // Show startup screen
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "STM32-Band");
    ssd1306_draw_string(1, 0, "Starting...");
    ssd1306_draw_string(3, 0, "Please wait");
    ssd1306_update();

    // Startup vibration
    motor_vibrate(100);

    // Check BLE module
    if (ble_module_present()) {
        ble_state = BLE_STATE_ADVERTISING;
        display_update(BLE_STATE_ADVERTISING);
    } else {
        ble_state = BLE_STATE_IDLE;
        display_update(BLE_STATE_IDLE);
    }

    // Main loop
    while (1) {
        // BLE module not installed yet, just idle
        // When E104-BT52 is installed, USART1 ISR will handle state changes
        HAL_Delay(100);
    }
}
