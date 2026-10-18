/**
 * STM32-Band 硬件测试程序
 *
 * 测试内容:
 *   1. OLED 显示 (SSD1306, I2C1, PB6/PB7)
 *   2. 马达震动 (PA6)
 *
 * 使用方法:
 *   在 CubeMX 中配置好 I2C1、GPIO 后，
 *   将本文件内容复制到 main.c 的 USER CODE 区域，替换原有逻辑。
 *   或者将 main_app() 替换为 test_main() 调用。
 */

#include "main.h"
#include "ssd1306.h"
#include "motor.h"

extern I2C_HandleTypeDef hi2c1;

// ─── 简单延时（避免依赖 HAL_Delay 的中断） ──────────────
static void delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

// ─── 测试 1: OLED 显示 ─────────────────────────────────
static void test_oled(void)
{
    ssd1306_config_t cfg = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x3C,
    };

    if (ssd1306_init(&cfg) != HAL_OK) {
        // 初始化失败，快速闪马达报警
        while (1) {
            motor_vibrate(50);
            delay_ms(200);
        }
    }

    // 测试 1.1: 全屏填充
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== OLED TEST ===");
    ssd1306_draw_string(1, 0, "128x32 SSD1306");
    ssd1306_draw_string(2, 0, "I2C1 PB6/PB7");
    ssd1306_draw_string(3, 0, "Addr: 0x3C");
    ssd1306_update();
    delay_ms(2000);

    // 测试 1.2: 字符覆盖
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "ABCDEFGHIJKLM");
    ssd1306_draw_string(1, 0, "NOPQRSTUVWXYZ");
    ssd1306_draw_string(2, 0, "0123456789");
    ssd1306_draw_string(3, 0, "!@#$%^&*()~");
    ssd1306_update();
    delay_ms(2000);

    // 测试 1.3: 逐行滚动效果
    for (int i = 0; i < 4; i++) {
        ssd1306_clear();
        ssd1306_draw_string(i, 0, ">>> SCROLL TEST");
        ssd1306_update();
        delay_ms(300);
    }
    delay_ms(500);

    // 测试 1.4: 像素绘制（画边框）
    ssd1306_clear();
    for (int x = 0; x < SSD1306_WIDTH; x++) {
        ssd1306_draw_pixel(x, 0, true);
        ssd1306_draw_pixel(x, SSD1306_HEIGHT - 1, true);
    }
    for (int y = 0; y < SSD1306_HEIGHT; y++) {
        ssd1306_draw_pixel(0, y, true);
        ssd1306_draw_pixel(SSD1306_WIDTH - 1, y, true);
    }
    ssd1306_draw_string(1, 6, "PIXEL OK!");
    ssd1306_update();
    delay_ms(1500);
}

// ─── 测试 2: 马达震动 ─────────────────────────────────
static void test_motor(void)
{
    motor_init();

    // 测试 2.1: 短震动
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== MOTOR TEST ===");
    ssd1306_draw_string(1, 0, "Short vibrate...");
    ssd1306_update();
    motor_vibrate(100);
    delay_ms(500);

    // 测试 2.2: 中等震动
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== MOTOR TEST ===");
    ssd1306_draw_string(1, 0, "Medium vibrate...");
    ssd1306_update();
    motor_vibrate(300);
    delay_ms(500);

    // 测试 2.3: 长震动
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== MOTOR TEST ===");
    ssd1306_draw_string(1, 0, "Long vibrate...");
    ssd1306_update();
    motor_vibrate(800);
    delay_ms(500);

    // 测试 2.4: 连续震动模式
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== MOTOR TEST ===");
    ssd1306_draw_string(1, 0, "Pattern: SOS");
    ssd1306_update();

    // SOS: ... --- ...
    for (int i = 0; i < 3; i++) { motor_vibrate(100); delay_ms(100); }  // ...
    delay_ms(200);
    for (int i = 0; i < 3; i++) { motor_vibrate(300); delay_ms(100); }  // ---
    delay_ms(200);
    for (int i = 0; i < 3; i++) { motor_vibrate(100); delay_ms(100); }  // ...
    delay_ms(500);
}

// ─── 测试入口 ──────────────────────────────────────────
void test_main(void)
{
    // 启动提示
    motor_init();
    motor_vibrate(50);  // 短响表示开始

    // 运行所有测试
    test_oled();
    test_motor();

    // 测试完成
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== ALL DONE ===");
    ssd1306_draw_string(1, 0, "OLED:  OK");
    ssd1306_draw_string(2, 0, "Motor: OK");
    ssd1306_update();

    motor_vibrate(100);
    delay_ms(100);
    motor_vibrate(100);

    // 保持显示
    while (1) {
        delay_ms(1000);
    }
}
