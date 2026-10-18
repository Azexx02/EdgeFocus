#include "e104_bt52.h"

#include <string.h>

static void clear_uart_errors(UART_HandleTypeDef *huart)
{
    __HAL_UART_CLEAR_FLAG(huart,
                          UART_CLEAR_OREF |
                          UART_CLEAR_FEF |
                          UART_CLEAR_NEF |
                          UART_CLEAR_PEF |
                          UART_CLEAR_IDLEF);
}

void e104_bt52_init(e104_bt52_t *dev,
                    UART_HandleTypeDef *huart,
                    GPIO_TypeDef *wake_port,
                    uint16_t wake_pin,
                    GPIO_TypeDef *reset_port,
                    uint16_t reset_pin,
                    GPIO_TypeDef *link_port,
                    uint16_t link_pin)
{
    if (dev == NULL) {
        return;
    }

    dev->huart = huart;
    dev->wake_port = wake_port;
    dev->wake_pin = wake_pin;
    dev->reset_port = reset_port;
    dev->reset_pin = reset_pin;
    dev->link_port = link_port;
    dev->link_pin = link_pin;
}

HAL_StatusTypeDef e104_bt52_uart_force_default(e104_bt52_t *dev)
{
    UART_HandleTypeDef *huart;
    HAL_StatusTypeDef status;

    if (dev == NULL || dev->huart == NULL) {
        return HAL_ERROR;
    }

    huart = dev->huart;
    huart->Init.BaudRate = E104_BT52_DEFAULT_BAUDRATE;
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    huart->Init.StopBits = UART_STOPBITS_1;
    huart->Init.Parity = UART_PARITY_NONE;
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    status = HAL_UART_Init(huart);
    if (status != HAL_OK) {
        return status;
    }
    status = HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8);
    if (status != HAL_OK) {
        return status;
    }
    status = HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8);
    if (status != HAL_OK) {
        return status;
    }
    return HAL_UARTEx_DisableFifoMode(huart);
}

void e104_bt52_wake(e104_bt52_t *dev)
{
    if (dev == NULL) {
        return;
    }

    HAL_GPIO_WritePin(dev->reset_port, dev->reset_pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(dev->wake_port, dev->wake_pin, GPIO_PIN_SET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(dev->wake_port, dev->wake_pin, GPIO_PIN_RESET);
    HAL_Delay(500U);
}

void e104_bt52_sleep(e104_bt52_t *dev)
{
    if (dev == NULL) {
        return;
    }

    HAL_GPIO_WritePin(dev->wake_port, dev->wake_pin, GPIO_PIN_SET);
    HAL_Delay(100U);
}

void e104_bt52_reset(e104_bt52_t *dev)
{
    if (dev == NULL) {
        return;
    }

    HAL_GPIO_WritePin(dev->wake_port, dev->wake_pin, GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(dev->reset_port, dev->reset_pin, GPIO_PIN_RESET);
    HAL_Delay(100U);
    HAL_GPIO_WritePin(dev->reset_port, dev->reset_pin, GPIO_PIN_SET);
    HAL_Delay(2500U);
}

uint8_t e104_bt52_is_linked(const e104_bt52_t *dev)
{
    if (dev == NULL || dev->link_port == NULL) {
        return 0U;
    }

    return (HAL_GPIO_ReadPin(dev->link_port, dev->link_pin) == GPIO_PIN_SET) ? 1U : 0U;
}

void e104_bt52_flush_rx(e104_bt52_t *dev)
{
    if (dev == NULL || dev->huart == NULL) {
        return;
    }

    clear_uart_errors(dev->huart);
    while (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_RXFNE)) {
        (void)dev->huart->Instance->RDR;
    }
}

e104_bt52_status_t e104_bt52_read(e104_bt52_t *dev,
                                  uint8_t *buf,
                                  size_t buf_size,
                                  size_t *out_len,
                                  uint32_t timeout_ms)
{
    uint32_t start;
    size_t len = 0U;

    if (dev == NULL || dev->huart == NULL || out_len == NULL) {
        return E104_BT52_STATUS_BAD_ARG;
    }

    *out_len = 0U;
    start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_RXFNE)) {
            uint8_t b = (uint8_t)(dev->huart->Instance->RDR & 0xFFU);
            if (buf != NULL && len < buf_size) {
                buf[len] = b;
            }
            len++;
        }

        if (__HAL_UART_GET_FLAG(dev->huart, UART_FLAG_ORE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_PE)) {
            clear_uart_errors(dev->huart);
        }
    }

    *out_len = (buf != NULL && len > buf_size) ? buf_size : len;
    return (len > 0U) ? E104_BT52_STATUS_OK : E104_BT52_STATUS_TIMEOUT;
}

e104_bt52_status_t e104_bt52_write(e104_bt52_t *dev,
                                   const uint8_t *data,
                                   size_t len,
                                   uint32_t timeout_ms)
{
    if (dev == NULL || dev->huart == NULL || data == NULL || len > UINT16_MAX) {
        return E104_BT52_STATUS_BAD_ARG;
    }

    if (HAL_UART_Transmit(dev->huart, (uint8_t *)data, (uint16_t)len, timeout_ms) != HAL_OK) {
        return E104_BT52_STATUS_UART_ERROR;
    }

    return E104_BT52_STATUS_OK;
}

e104_bt52_status_t e104_bt52_command(e104_bt52_t *dev,
                                     const char *cmd,
                                     uint8_t *response,
                                     size_t response_size,
                                     size_t *response_len,
                                     uint32_t timeout_ms)
{
    if (cmd == NULL) {
        return E104_BT52_STATUS_BAD_ARG;
    }

    return e104_bt52_command_bytes(dev,
                                   (const uint8_t *)cmd,
                                   strlen(cmd),
                                   response,
                                   response_size,
                                   response_len,
                                   timeout_ms);
}

e104_bt52_status_t e104_bt52_command_bytes(e104_bt52_t *dev,
                                           const uint8_t *cmd,
                                           size_t cmd_len,
                                           uint8_t *response,
                                           size_t response_size,
                                           size_t *response_len,
                                           uint32_t timeout_ms)
{
    e104_bt52_status_t status;

    if (dev == NULL || cmd == NULL || response_len == NULL) {
        return E104_BT52_STATUS_BAD_ARG;
    }

    *response_len = 0U;
    if (response != NULL && response_size > 0U) {
        memset(response, 0, response_size);
    }

    e104_bt52_flush_rx(dev);
    status = e104_bt52_write(dev, cmd, cmd_len, 100U);
    if (status != E104_BT52_STATUS_OK) {
        return status;
    }

    status = e104_bt52_read(dev, response, response_size, response_len, timeout_ms);
    if (status != E104_BT52_STATUS_OK) {
        return status;
    }
    if (e104_bt52_response_has_error(response, *response_len)) {
        return E104_BT52_STATUS_AT_ERROR;
    }

    return E104_BT52_STATUS_OK;
}

uint8_t e104_bt52_response_has_ok(const uint8_t *response, size_t response_len)
{
    if (response == NULL) {
        return 0U;
    }

    for (size_t i = 0U; i + 2U < response_len; i++) {
        if (response[i] == '+' && response[i + 1U] == 'O' && response[i + 2U] == 'K') {
            return 1U;
        }
    }
    return 0U;
}

uint8_t e104_bt52_response_has_error(const uint8_t *response, size_t response_len)
{
    if (response == NULL) {
        return 0U;
    }

    for (size_t i = 0U; i + 4U < response_len; i++) {
        if (response[i] == '+' &&
            response[i + 1U] == 'E' &&
            response[i + 2U] == 'R' &&
            response[i + 3U] == 'R' &&
            response[i + 4U] == '=') {
            return 1U;
        }
    }
    return 0U;
}

e104_bt52_status_t e104_bt52_enter_at_mode_if_linked(e104_bt52_t *dev,
                                                     uint8_t *response,
                                                     size_t response_size,
                                                     size_t *response_len)
{
    if (dev == NULL || response_len == NULL) {
        return E104_BT52_STATUS_BAD_ARG;
    }

    if (!e104_bt52_is_linked(dev)) {
        *response_len = 0U;
        return E104_BT52_STATUS_OK;
    }

    return e104_bt52_command(dev, "+++", response, response_size, response_len, 800U);
}

e104_bt52_status_t e104_bt52_configure_project_defaults(e104_bt52_t *dev,
                                                        uint8_t *response,
                                                        size_t response_size,
                                                        size_t *response_len)
{
    static const char *const cmds[] = {
        "AT+ROLE=0",
        "AT+ADV=1",
        "AT+TRANMD=1",
    };
    static const uint8_t uuid_svr_cmd[] = {
        'A', 'T', '+', 'U', 'U', 'I', 'D', 'S', 'V', 'R', '=',
        0x0CU, 0x18U,
    };
    static const uint8_t uuid_mast_cmd[] = {
        'A', 'T', '+', 'U', 'U', 'I', 'D', 'M', 'A', 'S', 'T', '=',
        0x56U, 0x2AU,
    };

    if (response_len == NULL) {
        return E104_BT52_STATUS_BAD_ARG;
    }

    for (size_t i = 0U; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        e104_bt52_status_t status = e104_bt52_command(dev,
                                                      cmds[i],
                                                      response,
                                                      response_size,
                                                      response_len,
                                                      800U);
        if (status != E104_BT52_STATUS_OK) {
            return status;
        }
        if (!e104_bt52_response_has_ok(response, *response_len)) {
            return E104_BT52_STATUS_AT_ERROR;
        }
    }

    if (e104_bt52_command_bytes(dev,
                                uuid_svr_cmd,
                                sizeof(uuid_svr_cmd),
                                response,
                                response_size,
                                response_len,
                                800U) != E104_BT52_STATUS_OK) {
        return E104_BT52_STATUS_AT_ERROR;
    }
    if (!e104_bt52_response_has_ok(response, *response_len)) {
        return E104_BT52_STATUS_AT_ERROR;
    }

    if (e104_bt52_command_bytes(dev,
                                uuid_mast_cmd,
                                sizeof(uuid_mast_cmd),
                                response,
                                response_size,
                                response_len,
                                800U) != E104_BT52_STATUS_OK) {
        return E104_BT52_STATUS_AT_ERROR;
    }
    if (!e104_bt52_response_has_ok(response, *response_len)) {
        return E104_BT52_STATUS_AT_ERROR;
    }

    return E104_BT52_STATUS_OK;
}
