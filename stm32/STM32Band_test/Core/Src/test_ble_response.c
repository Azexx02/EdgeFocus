/**
 * BLE 协议响应测试
 *
 * 按照 BLE_Signal_Protocol.md 验证以下功能：
 *   1. Alert (0x01) — 三种震动模式
 *      - 0x01: 连续缓慢短震 (3次, 间隔500ms)
 *      - 0x02: 长振 (800ms)
 *      - 0x03: 连续快速短振 (3次, 间隔100ms)
 *   2. TimeSync (0x02) — OLED 倒计时显示
 *   3. Text (0x03) — OLED 文字显示
 */

#include "main.h"
#include "ssd1306.h"
#include "motor.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

static void delay_ms(uint32_t ms) { HAL_Delay(ms); }

// ─── Alert 处理 (0x01) ──────────────────────────────────
// vibration: 振动模式 (1/2/3), level: 提醒级别 (1~4)
static void handle_alert(uint8_t vibration, uint8_t level)
{
    switch (vibration) {
        case 0x01: // 连续缓慢短震
            for (int i = 0; i < 3; i++) {
                motor_vibrate(300);
                delay_ms(500);
            }
            break;
        case 0x02: // 长振
            motor_vibrate(800);
            break;
        case 0x03: // 连续快速短振
            for (int i = 0; i < 3; i++) {
                motor_vibrate(300);
                delay_ms(100);
            }
            break;
        default:
            break;
    }
    ssd1306_show_alert(level);
}

// ─── TimeSync 处理 (0x02) ───────────────────────────────
static void handle_time_sync(uint8_t hi, uint8_t lo)
{
    uint16_t minutes = (hi << 8) | lo;
    ssd1306_show_countdown(minutes);
}

// ─── Text 处理 (0x03) ──────────────────────────────────
static void handle_text(const uint8_t *data, size_t len)
{
    char text[22]; // 128/6 = 21 字符 + '\0'
    size_t copy_len = len - 1; // skip byte 0 (type)
    if (copy_len > 21) copy_len = 21;
    memcpy(text, data + 1, copy_len);
    text[copy_len] = '\0';

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== TEXT MSG ===");
    ssd1306_draw_string(1, 0, text);
    ssd1306_update();
    delay_ms(3000);
}

// ─── 测试 1: 三种震动模式 ──────────────────────────────
static void test_alert_vibration(void)
{
    // 模式 0x01: 连续缓慢短震 (温和提醒)
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== ALERT TEST ===");
    ssd1306_draw_string(1, 0, "Pattern 0x01:");
    ssd1306_draw_string(2, 0, "Slow 3x buzz");
    ssd1306_update();
    delay_ms(1000);
    handle_alert(0x01, 2);
    delay_ms(1000);

    // 模式 0x02: 长振
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== ALERT TEST ===");
    ssd1306_draw_string(1, 0, "Pattern 0x02:");
    ssd1306_draw_string(2, 0, "Long buzz 800ms");
    ssd1306_update();
    delay_ms(1000);
    handle_alert(0x02, 3);
    delay_ms(1000);

    // 模式 0x03: 连续快速短振 (紧急)
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== ALERT TEST ===");
    ssd1306_draw_string(1, 0, "Pattern 0x03:");
    ssd1306_draw_string(2, 0, "Fast 3x buzz");
    ssd1306_update();
    delay_ms(1000);
    handle_alert(0x03, 4);
    delay_ms(1000);
}

// ─── 测试 2: OLED 倒计时显示 ──────────────────────────
static void test_countdown(void)
{
    // 模拟收到 TimeSync: {0x02, 0x00, 0x1E} = 30分钟
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== TIMESYNC ===");
    ssd1306_draw_string(1, 0, "Recv: 02 00 1E");
    ssd1306_draw_string(2, 0, "= 30 minutes");
    ssd1306_update();
    delay_ms(2000);

    handle_time_sync(0x00, 0x1E); // 30分钟
    delay_ms(3000);

    // 模拟收到 TimeSync: {0x02, 0x00, 0x0F} = 15分钟
    handle_time_sync(0x00, 0x0F); // 15分钟
    delay_ms(3000);

    // 模拟收到 TimeSync: {0x02, 0x00, 0x05} = 5分钟
    handle_time_sync(0x00, 0x05); // 5分钟
    delay_ms(3000);

    // 模拟收到 TimeSync: {0x02, 0x00, 0x00} = 时间到
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== TIMESYNC ===");
    ssd1306_draw_string(1, 0, "Recv: 02 00 00");
    ssd1306_draw_string(2, 0, "= Time up!");
    ssd1306_update();
    delay_ms(2000);

    handle_time_sync(0x00, 0x00); // 时间到
    delay_ms(3000);
}

// ─── 测试 3: OLED 文字显示 ──────────────────────────────
static void test_text_display(void)
{
    // 模拟收到 Text: {0x03, 'O', 'K'} = "OK"
    uint8_t msg_ok[] = {0x03, 'O', 'K'};
    handle_text(msg_ok, sizeof(msg_ok));

    // 模拟收到 Text: {0x03, 'B', 'r', 'e', 'a', 'k', ' ', 'T', 'i', 'm', 'e'}
    uint8_t msg_break[] = {0x03, 'B', 'r', 'e', 'a', 'k', ' ', 'T', 'i', 'm', 'e'};
    handle_text(msg_break, sizeof(msg_break));

    // 模拟收到 Text: {0x03, 'H', 'e', 'l', 'l', 'o', '!'}
    uint8_t msg_hello[] = {0x03, 'H', 'e', 'l', 'l', 'o', '!'};
    handle_text(msg_hello, sizeof(msg_hello));
}

// ─── 测试入口 ──────────────────────────────────────────
void test_ble_response_main(void)
{
    // 初始化
    motor_init();

    ssd1306_config_t cfg = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x3C,
    };
    if (ssd1306_init(&cfg) != HAL_OK) {
        // OLED 初始化失败，马达快闪报警
        while (1) {
            motor_vibrate(50);
            delay_ms(200);
        }
    }

    // 启动提示
    motor_vibrate(50);

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "BLE Response Test");
    ssd1306_draw_string(1, 0, "Protocol v1.0");
    ssd1306_draw_string(3, 0, "Starting...");
    ssd1306_update();
    delay_ms(2000);

    // 测试 1: 三种震动模式
    test_alert_vibration();

    // 测试 2: 倒计时显示
    test_countdown();

    // 测试 3: 文字显示
    test_text_display();

    // 全部测试完成
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== ALL DONE ===");
    ssd1306_draw_string(1, 0, "Vibration: OK");
    ssd1306_draw_string(2, 0, "Countdown: OK");
    ssd1306_draw_string(3, 0, "Text:      OK");
    ssd1306_update();

    motor_vibrate(100);
    delay_ms(100);
    motor_vibrate(100);

    while (1) {
        delay_ms(1000);
    }
}
