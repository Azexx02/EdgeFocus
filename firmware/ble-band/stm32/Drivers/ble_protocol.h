#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#define BLE_PROTOCOL_TYPE_ALERT     0x01U
#define BLE_PROTOCOL_TYPE_TIMESYNC  0x02U
#define BLE_PROTOCOL_TYPE_TEXT      0x03U

#define BLE_PROTOCOL_ALERT_NONE     0x00U
#define BLE_PROTOCOL_ALERT_SLOW     0x01U
#define BLE_PROTOCOL_ALERT_LONG     0x02U
#define BLE_PROTOCOL_ALERT_FAST     0x03U
#define BLE_PROTOCOL_ALERT_LEGACY   0xFFU

#define BLE_PROTOCOL_LEVEL_DEFAULT  1U

typedef enum {
    BLE_PROTOCOL_RESULT_IGNORED = 0,
    BLE_PROTOCOL_RESULT_ALERT,
    BLE_PROTOCOL_RESULT_TIMESYNC,
    BLE_PROTOCOL_RESULT_TEXT,
} ble_protocol_result_t;

typedef struct {
    uint8_t pattern;
    uint8_t level;
} ble_protocol_alert_t;

typedef struct {
    uint16_t minutes;
} ble_protocol_timesync_t;

typedef struct {
    const uint8_t *data;
    size_t len;
} ble_protocol_text_t;

typedef struct {
    ble_protocol_result_t type;
    union {
        ble_protocol_alert_t alert;
        ble_protocol_timesync_t timesync;
        ble_protocol_text_t text;
    } value;
} ble_protocol_message_t;

ble_protocol_result_t ble_protocol_parse(const uint8_t *data,
                                         size_t len,
                                         ble_protocol_message_t *message);

#endif
