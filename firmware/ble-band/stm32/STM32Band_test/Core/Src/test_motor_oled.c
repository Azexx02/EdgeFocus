/**
 * PWM 马达独立测试
 *
 * 测试内容:
 *   1. 不同占空比 (25% / 50% / 75% / 100%)
 *   2. motor_vibrate 接口 (短/中/长)
 *   3. 连续震动模式
 *
 * 每一步都在 OLED 显示当前状态，方便定位问题。
 */

#include "main.h"
#include "ssd1306.h"
#include "motor.h"

extern I2C_HandleTypeDef hi2c1;

static void delay_ms(uint32_t ms) { HAL_Delay(ms); }

// 将 uint32_t 转为 8 位十六进制字符串 "0x12345678"
static void u32_to_hex(char *buf, uint32_t val)
{
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        uint8_t nibble = (val >> (i * 4)) & 0xF;
        buf[2 + (7 - i)] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[10] = '\0';
}

// 显示 motor_init 前后的关键寄存器
static void diagnose_registers(const char *label)
{
    char buf[22];

    // GPIOA MODER (PA6 = bits 13:12)
    // 00=input, 01=output, 10=AF, 11=analog
    uint32_t moder = GPIOA->MODER;
    uint32_t pa6_mode = (moder >> 12) & 0x3;

    // GPIOA AFRL (PA6 = AFRL bits 27:24)
    uint32_t afrl = GPIOA->AFR[0];
    uint32_t pa6_af = (afrl >> 24) & 0xF;

    // RCC APBENR2 bit 17 = TIM16EN
    uint32_t apbenr2 = RCC->APBENR2;
    uint32_t tim16_en = (apbenr2 >> 17) & 0x1;

    // TIM16 registers
    uint32_t cr1  = TIM16->CR1;
    uint32_t ccer = TIM16->CCER;
    uint32_t bdtr = TIM16->BDTR;
    uint32_t ccr1 = TIM16->CCR1;

    // 第 1 页: 标题
    ssd1306_clear();
    ssd1306_draw_string(0, 0, label);

    // 第 2 页: GPIOA MODER + PA6 mode
    // MODER 值显示太长，只显示 PA6 的模式
    const char *mode_str;
    if (pa6_mode == 0) mode_str = "IN ";
    else if (pa6_mode == 1) mode_str = "OUT";
    else if (pa6_mode == 2) mode_str = "AF ";
    else mode_str = "AN ";
    ssd1306_draw_string(1, 0, "PA6:");
    ssd1306_draw_string(1, 30, mode_str);

    // AF 值
    buf[0] = 'A'; buf[1] = 'F'; buf[2] = '=';
    buf[3] = '0' + pa6_af;
    buf[4] = '\0';
    ssd1306_draw_string(1, 60, buf);

    // TIM16EN
    ssd1306_draw_string(1, 96, tim16_en ? "T16Y" : "T16N");

    // 第 3 页: TIM16 CR1/CCER/BDTR
    u32_to_hex(buf, cr1);
    ssd1306_draw_string(2, 0, buf);

    u32_to_hex(buf, ccer);
    ssd1306_draw_string(2, 80, buf);

    // 第 4 页: CCR1
    buf[0] = 'C'; buf[1] = 'C'; buf[2] = 'R'; buf[3] = '=';
    buf[4] = '0' + (ccr1 / 1000) % 10;
    buf[5] = '0' + (ccr1 / 100) % 10;
    buf[6] = '0' + (ccr1 / 10) % 10;
    buf[7] = '0' + ccr1 % 10;
    buf[8] = '\0';
    ssd1306_draw_string(3, 0, buf);

    // MOE bit
    ssd1306_draw_string(3, 72, (bdtr & (1 << 15)) ? "MOE=1" : "MOE=0");

    // CC1E bit
    ssd1306_draw_string(3, 108, (ccer & (1 << 0)) ? "E" : "X");

    ssd1306_update();
}

// ─── 测试 1: 不同占空比 ──────────────────────────────
static void test_duty_levels(void)
{
    uint8_t levels[] = {50, 75, 100};
    const char *names[] = {"50%", "75%", "100%"};

    for (int i = 0; i < 3; i++) {
        ssd1306_clear();
        ssd1306_draw_string(0, 0, "=== MOTOR PWM ===");
        ssd1306_draw_string(1, 0, "Duty test:");
        ssd1306_draw_string(2, 0, names[i]);
        ssd1306_draw_string(3, 0, "Vibrating 2s...");
        ssd1306_update();

        motor_set_duty(levels[i]);
        delay_ms(2000);

        motor_off();
        ssd1306_draw_string(3, 0, "OFF - wait 1s   ");
        ssd1306_update();
        delay_ms(1000);
    }
}

// ─── 测试 2: motor_vibrate 接口 ──────────────────────
static void test_vibrate_interface(void)
{
    struct { uint32_t ms; const char *name; } tests[] = {
        {100,  "100ms short"},
        {500,  "500ms medium"},
        {1000, "1000ms long"},
    };

    for (int i = 0; i < 3; i++) {
        ssd1306_clear();
        ssd1306_draw_string(0, 0, "=== MOTOR PWM ===");
        ssd1306_draw_string(1, 0, "vibrate() test:");
        ssd1306_draw_string(2, 0, tests[i].name);
        ssd1306_draw_string(3, 0, "Go!");
        ssd1306_update();

        delay_ms(500);
        motor_vibrate(tests[i].ms);

        ssd1306_draw_string(3, 0, "Done.           ");
        ssd1306_update();
        delay_ms(500);
    }
}

// ─── 测试 3: 连续震动模式 ─────────────────────────────
static void test_pattern(void)
{
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== MOTOR PWM ===");
    ssd1306_draw_string(1, 0, "Pattern: 3x slow");
    ssd1306_draw_string(2, 0, "500ms, gap 500ms");
    ssd1306_update();
    delay_ms(500);

    for (int i = 0; i < 3; i++) {
        motor_vibrate(500);
        delay_ms(500);
    }

    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== MOTOR PWM ===");
    ssd1306_draw_string(1, 0, "Pattern: 3x fast");
    ssd1306_draw_string(2, 0, "500ms, gap 200ms");
    ssd1306_update();
    delay_ms(500);

    for (int i = 0; i < 3; i++) {
        motor_vibrate(500);
        delay_ms(200);
    }
}

// ─── 测试入口 ──────────────────────────────────────────
void test_main(void)
{
    // 先初始化 OLED（motor_init 之前需要 OLED 显示诊断信息）
    ssd1306_config_t cfg = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x3C,
    };
    if (ssd1306_init(&cfg) != HAL_OK) {
        while (1) {
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
            HAL_Delay(50);
            HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
            HAL_Delay(200);
        }
    }

    // 诊断: motor_init 之前的寄存器状态
    diagnose_registers("BEFORE init");
    delay_ms(4000);

    // 执行 motor_init
    motor_init();

    // 诊断: motor_init 之后的寄存器状态
    diagnose_registers("AFTER init");
    delay_ms(4000);

    // 测试 1: 占空比
    test_duty_levels();

    // 测试 2: vibrate 接口
    test_vibrate_interface();

    // 测试 3: 连续模式
    test_pattern();

    // 完成
    ssd1306_clear();
    ssd1306_draw_string(0, 0, "=== ALL DONE ===");
    ssd1306_draw_string(1, 0, "Motor PWM: OK");
    ssd1306_draw_string(2, 0, "TIM16 CH1 PA6");
    ssd1306_draw_string(3, 0, "5kHz PWM");
    ssd1306_update();

    motor_vibrate(100);
    delay_ms(100);
    motor_vibrate(100);

    while (1) {
        delay_ms(1000);
    }
}
