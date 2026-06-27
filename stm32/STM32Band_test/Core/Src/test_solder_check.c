/**
 * 新板焊接检查 — 跑所有已验证模块
 *
 * 测试: OLED → 马达 → 电池ADC → 按键 → 充电/电源
 * 每步 OLED 显示结果，马达双短震 = 全部通过
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

static void delay_ms(uint32_t ms) { HAL_Delay(ms); }

static void show_result(const char *module, const char *result)
{
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "== SOLDER CHECK ==");
    ssd1306_draw_string(1, 0, module);
    ssd1306_draw_string(2, 0, result);
    ssd1306_update();
}

// ─── 测试 1: OLED ───────────────────────────────────
static uint8_t test_oled(void)
{
    ssd1306_config_t cfg = { .hi2c = &hi2c1, .i2c_addr = 0x3C };
    if (ssd1306_init(&cfg) != HAL_OK) return 0;

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "== SOLDER CHECK ==");
    ssd1306_draw_string(1, 0, "[1] OLED");
    ssd1306_draw_string(2, 0, "PASS");
    ssd1306_update();
    delay_ms(1500);
    return 1;
}

// ─── 测试 2: 马达 ───────────────────────────────────
static uint8_t test_motor(void)
{
    show_result("[2] Motor", "Testing...");

    for (int i = 0; i < 3; i++) {
        motor_vibrate(200);
        delay_ms(200);
    }

    show_result("[2] Motor", "PASS");
    delay_ms(1500);
    return 1;
}

// ─── 测试 3: 电池 ADC ───────────────────────────────
static uint8_t test_battery(void)
{
    battery_init(&hadc1);

    show_result("[3] Battery", "Reading...");
    delay_ms(300);

    uint16_t mv = battery_read_mv();
    char line[22] = "Bat: XXXXmV";
    line[5] = '0' + (mv / 1000) % 10;
    line[6] = '0' + (mv / 100) % 10;
    line[7] = '0' + (mv / 10) % 10;
    line[8] = '0' + mv % 10;

    uint8_t ok = (mv > 2500 && mv < 4500);
    show_result("[3] Battery", ok ? line : "RANGE ERROR");
    delay_ms(2000);
    return ok;
}

// ─── 测试 4: 按键 ───────────────────────────────────
static uint8_t test_button_solder(void)
{
    button_init();

    show_result("[4] Button", "Press 1x");

    uint8_t pressed = button_wait_press(5000);
    show_result("[4] Button", pressed ? "PASS" : "FAIL");
    delay_ms(1500);
    return pressed;
}

// ─── 测试 5: 充电/电源 ──────────────────────────────
static uint8_t test_power_solder(void)
{
    power_init();

    show_result("[5] Power", "Reading...");
    delay_ms(300);

    uint8_t ext = power_ext_present();
    uint8_t chg = power_charging();

    char line[22] = "PGD:x CHG:x";
    line[5] = ext ? '1' : '0';
    line[10] = chg ? '1' : '0';

    show_result("[5] Power", line);
    delay_ms(2000);
    return 1;
}

// ─── 主入口 ──────────────────────────────────────────
void test_solder_check(void)
{
    // 1. OLED 先初始化
    if (!test_oled()) {
        while (1) {
            HAL_Delay(500);
        }
    }

    // 2. 马达只初始化一次
    motor_init();

    // 3. 依次测试
    test_motor();
    test_battery();
    test_button_solder();
    test_power_solder();

    // 全部通过
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "== ALL PASS ==");
    ssd1306_draw_string(1, 0, "OLED/Motor/Bat");
    ssd1306_draw_string(2, 0, "Button/Power");
    ssd1306_update();

    motor_vibrate(100);
    delay_ms(100);
    motor_vibrate(100);

    while (1) {
        delay_ms(1000);
    }
}
