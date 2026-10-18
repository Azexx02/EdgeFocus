#include "ble_band.h"

#include "ble_protocol.h"
#include <string.h>

static void set_state(ble_band_t *band, ble_band_state_t state)
{
    if (band->state == state) {
        return;
    }

    band->state = state;
    if (band->callbacks.state_changed != NULL) {
        band->callbacks.state_changed(state, band->callbacks.user);
    }
}

static void report_error(ble_band_t *band, ble_band_result_t error)
{
    if (band->callbacks.error != NULL) {
        band->callbacks.error(error, band->callbacks.user);
    }
}

static uint8_t response_ok(const uint8_t *response, size_t response_len)
{
    return e104_bt52_response_has_ok(response, response_len);
}

static void report_config_step(ble_band_t *band, const char *cmd, ble_band_result_t result)
{
    if (band->callbacks.config_step != NULL) {
        band->callbacks.config_step(cmd,
                                    result,
                                    band->rx_buf,
                                    band->rx_len,
                                    band->callbacks.user);
    }
}

static ble_band_result_t run_config_command(ble_band_t *band, const char *cmd)
{
    e104_bt52_status_t status;
    ble_band_result_t result = BLE_BAND_OK;

    status = e104_bt52_command(&band->e104,
                               cmd,
                               band->rx_buf,
                               sizeof(band->rx_buf),
                               &band->rx_len,
                               800U);
    if (status != E104_BT52_STATUS_OK) {
        result = BLE_BAND_E104_ERROR;
        report_config_step(band, cmd, result);
        return result;
    }
    if (!response_ok(band->rx_buf, band->rx_len)) {
        result = BLE_BAND_CONFIG_ERROR;
        report_config_step(band, cmd, result);
        return result;
    }
    report_config_step(band, cmd, result);
    return result;
}

static ble_band_result_t run_config_command_bytes(ble_band_t *band,
                                                  const char *label,
                                                  const uint8_t *cmd,
                                                  size_t cmd_len)
{
    e104_bt52_status_t status;
    ble_band_result_t result = BLE_BAND_OK;

    status = e104_bt52_command_bytes(&band->e104,
                                     cmd,
                                     cmd_len,
                                     band->rx_buf,
                                     sizeof(band->rx_buf),
                                     &band->rx_len,
                                     800U);
    if (status != E104_BT52_STATUS_OK) {
        result = BLE_BAND_E104_ERROR;
        report_config_step(band, label, result);
        return result;
    }
    if (!response_ok(band->rx_buf, band->rx_len)) {
        result = BLE_BAND_CONFIG_ERROR;
        report_config_step(band, label, result);
        return result;
    }

    report_config_step(band, label, result);
    return result;
}

static void handle_protocol_message(ble_band_t *band, const ble_protocol_message_t *message)
{
    switch (message->type) {
        case BLE_PROTOCOL_RESULT_ALERT:
            if (band->callbacks.alert != NULL) {
                band->callbacks.alert(message->value.alert.pattern,
                                      message->value.alert.level,
                                      band->callbacks.user);
            }
            break;

        case BLE_PROTOCOL_RESULT_TIMESYNC:
            if (band->callbacks.time_sync != NULL) {
                band->callbacks.time_sync(message->value.timesync.minutes, band->callbacks.user);
            }
            break;

        case BLE_PROTOCOL_RESULT_TEXT:
            if (band->callbacks.text != NULL) {
                band->callbacks.text(message->value.text.data,
                                     message->value.text.len,
                                     band->callbacks.user);
            }
            break;

        default:
            break;
    }
}

static uint8_t is_status_text(const uint8_t *data, size_t len)
{
    for (size_t i = 0U; i + 3U < len; i++) {
        if (data[i] == 'S' && data[i + 1U] == 'T' && data[i + 2U] == 'A' && data[i + 3U] == ':') {
            return 1U;
        }
    }
    return 0U;
}

void ble_band_init(ble_band_t *band,
                   UART_HandleTypeDef *uart,
                   GPIO_TypeDef *wake_port,
                   uint16_t wake_pin,
                   GPIO_TypeDef *reset_port,
                   uint16_t reset_pin,
                   GPIO_TypeDef *link_port,
                   uint16_t link_pin,
                   const ble_band_callbacks_t *callbacks)
{
    if (band == NULL) {
        return;
    }

    memset(band, 0, sizeof(*band));
    e104_bt52_init(&band->e104,
                   uart,
                   wake_port,
                   wake_pin,
                   reset_port,
                   reset_pin,
                   link_port,
                   link_pin);
    if (callbacks != NULL) {
        band->callbacks = *callbacks;
    }
    band->state = BLE_BAND_STATE_OFFLINE;
}

ble_band_result_t ble_band_configure(ble_band_t *band)
{
    ble_band_result_t result;
    e104_bt52_status_t at_status;

    if (band == NULL) {
        return BLE_BAND_BAD_ARG;
    }

    band->configured = 0U;
    set_state(band, BLE_BAND_STATE_OFFLINE);

    if (e104_bt52_uart_force_default(&band->e104) != HAL_OK) {
        report_error(band, BLE_BAND_E104_ERROR);
        return BLE_BAND_E104_ERROR;
    }

    e104_bt52_wake(&band->e104);
    at_status = e104_bt52_enter_at_mode_if_linked(&band->e104,
                                                  band->rx_buf,
                                                  sizeof(band->rx_buf),
                                                  &band->rx_len);
    report_config_step(band, "+++", at_status == E104_BT52_STATUS_OK ? BLE_BAND_OK : BLE_BAND_E104_ERROR);
    if (at_status != E104_BT52_STATUS_OK) {
        report_error(band, BLE_BAND_E104_ERROR);
        return BLE_BAND_E104_ERROR;
    }

    result = run_config_command(band, "AT");
    if (result != BLE_BAND_OK) {
        report_error(band, result);
        return result;
    }

    result = run_config_command(band, "AT+ROLE=0");
    if (result == BLE_BAND_OK) {
        result = run_config_command(band, "AT+ADV=1");
    }
    if (result == BLE_BAND_OK) {
        result = run_config_command(band, "AT+TRANMD=1");
    }
    if (result == BLE_BAND_OK) {
        result = run_config_command(band, "AT+ONSLEEP=0");
    }
    if (result == BLE_BAND_OK) {
        static const uint8_t name_cmd[] = {
            'A', 'T', '+', 'N', 'A', 'M', 'E', '=',
            'E', 'd', 'g', 'e', 'f', 'o', 'c', 'u', 's', 'B', 'a', 'n', 'd',
        };
        result = run_config_command_bytes(band,
                                          "AT+NAME=Edgefocus",
                                          name_cmd,
                                          sizeof(name_cmd));
    }
    if (result == BLE_BAND_OK) {
        static const uint8_t uuid_svr_cmd[] = {
            'A', 'T', '+', 'U', 'U', 'I', 'D', 'S', 'V', 'R', '=',
            0x0CU, 0x18U,
        };
        result = run_config_command_bytes(band,
                                          "UUIDSVR raw 0C18",
                                          uuid_svr_cmd,
                                          sizeof(uuid_svr_cmd));
    }
    if (result == BLE_BAND_OK) {
        static const uint8_t uuid_mast_cmd[] = {
            'A', 'T', '+', 'U', 'U', 'I', 'D', 'M', 'A', 'S', 'T', '=',
            0x56U, 0x2AU,
        };
        result = run_config_command_bytes(band,
                                          "UUIDMAST raw 562A",
                                          uuid_mast_cmd,
                                          sizeof(uuid_mast_cmd));
    }
    if (result != BLE_BAND_OK) {
        report_error(band, result);
        return result;
    }

    (void)e104_bt52_command(&band->e104,
                            "AT+RESET",
                            band->rx_buf,
                            sizeof(band->rx_buf),
                            &band->rx_len,
                            500U);
    HAL_Delay(2500U);
    e104_bt52_wake(&band->e104);
    e104_bt52_flush_rx(&band->e104);

    band->configured = 1U;
    set_state(band, e104_bt52_is_linked(&band->e104) ? BLE_BAND_STATE_CONNECTED : BLE_BAND_STATE_ADVERTISING);
    return BLE_BAND_OK;
}

void ble_band_poll(ble_band_t *band, uint32_t poll_ms)
{
    e104_bt52_status_t status;
    ble_protocol_message_t message;
    ble_band_state_t link_state;

    if (band == NULL || !band->configured) {
        return;
    }

    link_state = e104_bt52_is_linked(&band->e104) ? BLE_BAND_STATE_CONNECTED : BLE_BAND_STATE_ADVERTISING;
    set_state(band, link_state);

    status = e104_bt52_read(&band->e104,
                            band->rx_buf,
                            sizeof(band->rx_buf),
                            &band->rx_len,
                            poll_ms);
    if (status != E104_BT52_STATUS_OK || band->rx_len == 0U) {
        return;
    }

    if (is_status_text(band->rx_buf, band->rx_len)) {
        return;
    }

    if (ble_protocol_parse(band->rx_buf, band->rx_len, &message) != BLE_PROTOCOL_RESULT_IGNORED) {
        handle_protocol_message(band, &message);
    } else if (band->callbacks.raw_rx != NULL) {
        band->callbacks.raw_rx(band->rx_buf, band->rx_len, band->callbacks.user);
    }
}

ble_band_state_t ble_band_state(const ble_band_t *band)
{
    return (band != NULL) ? band->state : BLE_BAND_STATE_OFFLINE;
}

uint8_t ble_band_is_configured(const ble_band_t *band)
{
    return (band != NULL) ? band->configured : 0U;
}
