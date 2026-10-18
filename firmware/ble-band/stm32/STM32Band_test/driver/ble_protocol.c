#include "ble_protocol.h"

ble_protocol_result_t ble_protocol_parse(const uint8_t *data,
                                         size_t len,
                                         ble_protocol_message_t *message)
{
    if (message != NULL) {
        message->type = BLE_PROTOCOL_RESULT_IGNORED;
    }

    if (data == NULL || len == 0U || message == NULL) {
        return BLE_PROTOCOL_RESULT_IGNORED;
    }

    if (len == 1U) {
        message->type = BLE_PROTOCOL_RESULT_ALERT;
        message->value.alert.pattern = BLE_PROTOCOL_ALERT_LEGACY;
        message->value.alert.level = BLE_PROTOCOL_LEVEL_DEFAULT;
        return message->type;
    }

    switch (data[0]) {
        case BLE_PROTOCOL_TYPE_ALERT:
            message->type = BLE_PROTOCOL_RESULT_ALERT;
            message->value.alert.pattern = data[1];
            message->value.alert.level = (len >= 3U) ? data[2] : BLE_PROTOCOL_LEVEL_DEFAULT;
            return message->type;

        case BLE_PROTOCOL_TYPE_TIMESYNC:
            if (len >= 3U) {
                message->type = BLE_PROTOCOL_RESULT_TIMESYNC;
                message->value.timesync.minutes = ((uint16_t)data[1] << 8) | (uint16_t)data[2];
                return message->type;
            }
            break;

        case BLE_PROTOCOL_TYPE_TEXT:
            if (len >= 2U) {
                message->type = BLE_PROTOCOL_RESULT_TEXT;
                message->value.text.data = &data[1];
                message->value.text.len = len - 1U;
                return message->type;
            }
            break;

        default:
            break;
    }

    message->type = BLE_PROTOCOL_RESULT_ALERT;
    message->value.alert.pattern = BLE_PROTOCOL_ALERT_LEGACY;
    message->value.alert.level = BLE_PROTOCOL_LEVEL_DEFAULT;
    return message->type;
}
