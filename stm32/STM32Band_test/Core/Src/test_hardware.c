/**
 * BLE 模块测试 — 按键切换模式
 *
 * 每个测试停留屏幕，方便测量。
 * 短按切换，长按 2 秒复位。
 */

#include "main.h"
#include "ssd1306.h"
#include "motor.h"
#include "battery.h"
#include "button.h"
#include "power.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c1;
extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart1;

static volatile uint8_t test_index = 0;
#define TEST_COUNT 3

static void delay_ms(uint32_t ms) { HAL_Delay(ms); }

static void show_nav(uint8_t cur)
{
    char buf[8] = { '[', '0' + cur + 1, '/', '0' + TEST_COUNT, ']', ' ', 'c', 'k' };
    ssd1306_draw_string(3, 0, buf);
}

// ─── 测试 0: BLE Link ───────────────────────────────
static void run_test_link(void)
{
    int link = HAL_GPIO_ReadPin(Bluetooth_connection_status_GPIO_Port, Bluetooth_connection_status_Pin);

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "[1] BLE Link");
    ssd1306_draw_string(1, 0, link ? "PB0 = HIGH" : "PB0 = LOW");
    ssd1306_draw_string(2, 0, link ? "Connected" : "No connection");
    show_nav(0);
    ssd1306_update();
}

// ─── 测试 1: BLE Reset ──────────────────────────────
static void run_test_reset(void)
{
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "[2] BLE Reset");
    ssd1306_draw_string(1, 0, "RST LOW 500ms...");
    show_nav(1);
    ssd1306_update();

    HAL_GPIO_WritePin(Bluetooth_reset_GPIO_Port, Bluetooth_reset_Pin, GPIO_PIN_RESET);
    delay_ms(500);
    HAL_GPIO_WritePin(Bluetooth_reset_GPIO_Port, Bluetooth_reset_Pin, GPIO_PIN_SET);
    delay_ms(1000);

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "[2] BLE Reset");
    ssd1306_draw_string(1, 0, "RST: LOW -> HIGH");
    ssd1306_draw_string(2, 0, "Module restarted");
    show_nav(1);
    ssd1306_update();
}

// ─── 测试 2: BLE UART ───────────────────────────────
static void run_test_uart(void)
{
    // 1. WKP 唤醒: 拉高100ms再拉低(下降沿唤醒)
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "[3] BLE UART");
    ssd1306_draw_string(1, 0, "WKP wake...");
    show_nav(2);
    ssd1306_update();

    HAL_GPIO_WritePin(Bluetooth_wake_up_GPIO_Port, Bluetooth_wake_up_Pin, GPIO_PIN_SET);
    delay_ms(100);
    HAL_GPIO_WritePin(Bluetooth_wake_up_GPIO_Port, Bluetooth_wake_up_Pin, GPIO_PIN_RESET);
    delay_ms(200);

    // 2. 复位模块
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "[3] BLE UART");
    ssd1306_draw_string(1, 0, "Reset module...");
    show_nav(2);
    ssd1306_update();

    HAL_GPIO_WritePin(Bluetooth_reset_GPIO_Port, Bluetooth_reset_Pin, GPIO_PIN_RESET);
    delay_ms(500);
    HAL_GPIO_WritePin(Bluetooth_reset_GPIO_Port, Bluetooth_reset_Pin, GPIO_PIN_SET);
    delay_ms(1000);

    // 3. 发送 AT
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "[3] BLE UART");
    ssd1306_draw_string(1, 0, "Sending AT...");
    show_nav(2);
    ssd1306_update();

    HAL_UART_Transmit(&huart1, (uint8_t *)"AT", 2, 200);

    // 4. 等待回复
    uint8_t rx_buf[64];
    memset(rx_buf, 0, sizeof(rx_buf));
    HAL_StatusTypeDef ret = HAL_UART_Receive(&huart1, rx_buf, sizeof(rx_buf) - 1, 3000);

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "[3] BLE UART");
    if (ret == HAL_OK && rx_buf[0] != 0) {
        char line[22];
        size_t len = strlen((char *)rx_buf);
        if (len > 21) len = 21;
        memcpy(line, rx_buf, len);
        line[len] = '\0';
        ssd1306_draw_string(1, 0, "Got:");
        ssd1306_draw_string(2, 0, line);
    } else {
        ssd1306_draw_string(1, 0, "No response");
        ssd1306_draw_string(2, 0, "Check PA9 wave");
    }
    show_nav(2);
    ssd1306_update();
}

// ─── 主循环 ──────────────────────────────────────────
void test_hardware_main(void)
{
    ssd1306_config_t cfg = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x3C,
    };
    if (ssd1306_init(&cfg) != HAL_OK) {
        while (1) {
            motor_init();
            motor_vibrate(50);
            HAL_Delay(200);
        }
    }

    motor_init();
    button_init();

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "BLE Test");
    ssd1306_draw_string(1, 0, "Click to start");
    ssd1306_draw_string(2, 0, "Long 2s = Reset");
    ssd1306_update();
    delay_ms(1000);

    while (1) {
        switch (test_index) {
            case 0: run_test_link();   break;
            case 1: run_test_reset();  break;
            case 2: run_test_uart();   break;
        }

        // 等待按键
        while (1) {
            uint8_t pressed = button_wait_press(5000);
            if (pressed) {
                // 长按检测
                uint32_t press_time = HAL_GetTick();
                while (button_read()) {
                    if ((HAL_GetTick() - press_time) > 2000) {
                        ssd1306_clear();
                        ssd1306_draw_string(0, 0, "RESET");
                        ssd1306_draw_string(1, 0, "Rebooting...");
                        ssd1306_update();
                        delay_ms(500);
                        NVIC_SystemReset();
                    }
                }
                // 短按: 切换
                test_index = (test_index + 1) % TEST_COUNT;
                motor_vibrate(30);
                break;
            }
        }
    }
}
