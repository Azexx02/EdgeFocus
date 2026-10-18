#ifndef E104_BT52_H
#define E104_BT52_H

#include "stm32g0xx_hal.h"
#include <stddef.h>
#include <stdint.h>

#define E104_BT52_SERVICE_UUID      "180C"
#define E104_BT52_WRITE_UUID        "2A56"
#define E104_BT52_DEFAULT_BAUDRATE  115200UL
#define E104_BT52_RX_BUF_SIZE       64U

typedef enum {
    E104_BT52_STATUS_OK = 0,
    E104_BT52_STATUS_TIMEOUT,
    E104_BT52_STATUS_UART_ERROR,
    E104_BT52_STATUS_AT_ERROR,
    E104_BT52_STATUS_BAD_ARG,
} e104_bt52_status_t;

typedef struct {
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *wake_port;
    uint16_t wake_pin;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
    GPIO_TypeDef *link_port;
    uint16_t link_pin;
} e104_bt52_t;

void e104_bt52_init(e104_bt52_t *dev,
                    UART_HandleTypeDef *huart,
                    GPIO_TypeDef *wake_port,
                    uint16_t wake_pin,
                    GPIO_TypeDef *reset_port,
                    uint16_t reset_pin,
                    GPIO_TypeDef *link_port,
                    uint16_t link_pin);

HAL_StatusTypeDef e104_bt52_uart_force_default(e104_bt52_t *dev);
void e104_bt52_wake(e104_bt52_t *dev);
void e104_bt52_sleep(e104_bt52_t *dev);
void e104_bt52_reset(e104_bt52_t *dev);
uint8_t e104_bt52_is_linked(const e104_bt52_t *dev);

void e104_bt52_flush_rx(e104_bt52_t *dev);
e104_bt52_status_t e104_bt52_read(e104_bt52_t *dev,
                                  uint8_t *buf,
                                  size_t buf_size,
                                  size_t *out_len,
                                  uint32_t timeout_ms);
e104_bt52_status_t e104_bt52_write(e104_bt52_t *dev,
                                   const uint8_t *data,
                                   size_t len,
                                   uint32_t timeout_ms);
e104_bt52_status_t e104_bt52_command(e104_bt52_t *dev,
                                     const char *cmd,
                                     uint8_t *response,
                                     size_t response_size,
                                     size_t *response_len,
                                     uint32_t timeout_ms);
e104_bt52_status_t e104_bt52_command_bytes(e104_bt52_t *dev,
                                           const uint8_t *cmd,
                                           size_t cmd_len,
                                           uint8_t *response,
                                           size_t response_size,
                                           size_t *response_len,
                                           uint32_t timeout_ms);

uint8_t e104_bt52_response_has_ok(const uint8_t *response, size_t response_len);
uint8_t e104_bt52_response_has_error(const uint8_t *response, size_t response_len);

e104_bt52_status_t e104_bt52_enter_at_mode_if_linked(e104_bt52_t *dev,
                                                     uint8_t *response,
                                                     size_t response_size,
                                                     size_t *response_len);
e104_bt52_status_t e104_bt52_configure_project_defaults(e104_bt52_t *dev,
                                                        uint8_t *response,
                                                        size_t response_size,
                                                        size_t *response_len);

#endif
