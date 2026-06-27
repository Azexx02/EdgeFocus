#include "main.h"
#include "motor.h"
#include "ssd1306.h"
#include <stdint.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;

#define RX_BUF_SIZE 32U

static uint8_t rx_buf[RX_BUF_SIZE];
static uint8_t rx_len;
static uint8_t oled_ok;
static const char *last_cmd;

static void delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

static void show_line(uint8_t row, const char *text)
{
    char buf[22];
    size_t n = strlen(text);

    if (!oled_ok) {
        return;
    }

    if (n > 21U) {
        n = 21U;
    }
    memcpy(buf, text, n);
    buf[n] = '\0';
    ssd1306_draw_string(row, 0, buf);
}

static void show4(const char *a, const char *b, const char *c, const char *d)
{
    if (!oled_ok) {
        return;
    }

    ssd1306_clear();
    show_line(0, a);
    show_line(1, b);
    show_line(2, c);
    show_line(3, d);
    ssd1306_update();
}

static char hex_digit(uint8_t value)
{
    value &= 0x0FU;
    return (value < 10U) ? (char)('0' + value) : (char)('A' + value - 10U);
}

static void make_rx_count(char *out)
{
    out[0] = 'R';
    out[1] = 'X';
    out[2] = '=';
    out[3] = (char)('0' + (rx_len / 10U));
    out[4] = (char)('0' + (rx_len % 10U));
    out[5] = '\0';
}

static void make_ascii(char *out, uint8_t out_size)
{
    uint8_t n = rx_len;

    if (out_size == 0U) {
        return;
    }

    if (n > out_size - 1U) {
        n = (uint8_t)(out_size - 1U);
    }

    for (uint8_t i = 0U; i < n; i++) {
        uint8_t b = rx_buf[i];
        out[i] = (b >= 0x20U && b <= 0x7EU) ? (char)b : '.';
    }
    out[n] = '\0';
}

static void make_hex(char *out, uint8_t out_size)
{
    uint8_t pos = 0U;

    if (out_size < 5U) {
        if (out_size > 0U) {
            out[0] = '\0';
        }
        return;
    }

    out[pos++] = 'H';
    out[pos++] = 'E';
    out[pos++] = 'X';
    out[pos++] = ' ';

    for (uint8_t i = 0U; i < rx_len && pos + 3U < out_size; i++) {
        uint8_t b = rx_buf[i];
        out[pos++] = hex_digit((uint8_t)(b >> 4));
        out[pos++] = hex_digit(b);
        if (pos + 1U < out_size) {
            out[pos++] = ' ';
        }
    }

    if (pos > 0U && out[pos - 1U] == ' ') {
        pos--;
    }
    out[pos] = '\0';
}

static void uart1_restore_115200(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    HAL_UART_Init(&huart1);
    HAL_UARTEx_SetTxFifoThreshold(&huart1, UART_TXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_SetRxFifoThreshold(&huart1, UART_RXFIFO_THRESHOLD_1_8);
    HAL_UARTEx_DisableFifoMode(&huart1);
}

static void ble_prepare_for_scope(void)
{
    HAL_GPIO_WritePin(Bluetooth_reset_GPIO_Port, Bluetooth_reset_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(Bluetooth_wake_up_GPIO_Port, Bluetooth_wake_up_Pin, GPIO_PIN_SET);
    delay_ms(150);
    HAL_GPIO_WritePin(Bluetooth_wake_up_GPIO_Port, Bluetooth_wake_up_Pin, GPIO_PIN_RESET);
    delay_ms(500);
}

static void usart_clear_rx_state(void)
{
    rx_len = 0U;
    memset(rx_buf, 0, sizeof(rx_buf));

    __HAL_UART_CLEAR_FLAG(&huart1,
                          UART_CLEAR_OREF |
                          UART_CLEAR_FEF |
                          UART_CLEAR_NEF |
                          UART_CLEAR_PEF |
                          UART_CLEAR_IDLEF);

    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXFNE)) {
        (void)huart1.Instance->RDR;
    }
}

static void usart_poll_rx(uint32_t duration_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < duration_ms) {
        if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXFNE)) {
            uint8_t b = (uint8_t)(huart1.Instance->RDR & 0xFFU);
            if (rx_len < RX_BUF_SIZE) {
                rx_buf[rx_len++] = b;
            }
        }

        if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE)) {
            __HAL_UART_CLEAR_FLAG(&huart1,
                                  UART_CLEAR_OREF |
                                  UART_CLEAR_FEF |
                                  UART_CLEAR_NEF |
                                  UART_CLEAR_PEF);
        }
    }
}

static uint8_t rx_has_ok(void)
{
    for (uint8_t i = 0U; i + 2U < rx_len; i++) {
        if (rx_buf[i] == '+' && rx_buf[i + 1U] == 'O' && rx_buf[i + 2U] == 'K') {
            return 1U;
        }
    }
    return 0U;
}

static void send_at_frame(void)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)last_cmd, strlen(last_cmd), 100U);
}

static void signal_result(void)
{
    if (rx_has_ok()) {
        motor_vibrate(700);
        return;
    }

    if (rx_len > 0U) {
        motor_vibrate(120);
        delay_ms(180);
        motor_vibrate(120);
        return;
    }

    for (uint8_t i = 0U; i < 3U; i++) {
        motor_vibrate(80);
        delay_ms(160);
    }
}

static void show_result(void)
{
    char count[8] = {0};
    char ascii[22] = {0};
    char hex[22] = {0};

    make_rx_count(count);
    make_ascii(ascii, sizeof(ascii));
    make_hex(hex, sizeof(hex));

    if (rx_has_ok()) {
        show4(last_cmd, "+OK found", ascii, count);
    } else if (rx_len > 0U) {
        show4(last_cmd, "RX no +OK", ascii, hex);
    } else {
        show4(last_cmd, "RX empty", "AT sent", "scope PA9/PA10");
    }
}

void test_hardware_main(void)
{
    ssd1306_config_t cfg = {
        .hi2c = &hi2c1,
        .i2c_addr = 0x3C,
    };

    oled_ok = (ssd1306_init(&cfg) == HAL_OK);

    motor_init();
    uart1_restore_115200();
    ble_prepare_for_scope();

    show4("E104 AT query", "register RX works", "cycling commands", "");
    motor_vibrate(100);
    delay_ms(500);

    static const char *cmds[] = {
        "AT",
        "AT+BAUD?",
        "AT+PARI?",
        "AT+DATABITS?",
        "AT+ROLE?",
        "AT+ADV?",
        "AT+TRANMD?",
        "AT+UUIDSVR?",
        "AT+UUIDMAST?",
    };
    uint8_t cmd_index = 0U;

    while (1) {
        last_cmd = cmds[cmd_index];
        usart_clear_rx_state();
        send_at_frame();
        usart_poll_rx(500U);
        show_result();
        signal_result();
        cmd_index = (uint8_t)((cmd_index + 1U) % (sizeof(cmds) / sizeof(cmds[0])));
        delay_ms(1800);
    }
}
