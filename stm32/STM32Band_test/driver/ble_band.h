#ifndef BLE_BAND_H
#define BLE_BAND_H

#include "e104_bt52.h"
#include <stddef.h>
#include <stdint.h>

#define BLE_BAND_SERVICE_UUID  E104_BT52_SERVICE_UUID
#define BLE_BAND_WRITE_UUID    E104_BT52_WRITE_UUID

typedef enum {
    BLE_BAND_STATE_OFFLINE = 0,
    BLE_BAND_STATE_ADVERTISING,
    BLE_BAND_STATE_CONNECTED,
} ble_band_state_t;

typedef enum {
    BLE_BAND_OK = 0,
    BLE_BAND_E104_ERROR,
    BLE_BAND_CONFIG_ERROR,
    BLE_BAND_BAD_ARG,
} ble_band_result_t;

typedef struct {
    void (*state_changed)(ble_band_state_t state, void *user);
    void (*config_step)(const char *cmd,
                        ble_band_result_t result,
                        const uint8_t *response,
                        size_t response_len,
                        void *user);
    void (*alert)(uint8_t pattern, uint8_t level, void *user);
    void (*time_sync)(uint16_t minutes, void *user);
    void (*text)(const uint8_t *data, size_t len, void *user);
    void (*raw_rx)(const uint8_t *data, size_t len, void *user);
    void (*error)(ble_band_result_t error, void *user);
    void *user;
} ble_band_callbacks_t;

typedef struct {
    e104_bt52_t e104;
    ble_band_callbacks_t callbacks;
    ble_band_state_t state;
    uint8_t configured;
    uint8_t rx_buf[E104_BT52_RX_BUF_SIZE];
    size_t rx_len;
} ble_band_t;

void ble_band_init(ble_band_t *band,
                   UART_HandleTypeDef *uart,
                   GPIO_TypeDef *wake_port,
                   uint16_t wake_pin,
                   GPIO_TypeDef *reset_port,
                   uint16_t reset_pin,
                   GPIO_TypeDef *link_port,
                   uint16_t link_pin,
                   const ble_band_callbacks_t *callbacks);

ble_band_result_t ble_band_configure(ble_band_t *band);
void ble_band_poll(ble_band_t *band, uint32_t poll_ms);
ble_band_state_t ble_band_state(const ble_band_t *band);
uint8_t ble_band_is_configured(const ble_band_t *band);

#endif
