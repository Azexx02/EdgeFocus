#ifndef GIMBAL_PROTOCOL_H
#define GIMBAL_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIMBAL_PROTOCOL_VERSION "3.0"
#define GIMBAL_PROTOCOL_MAX_LINE 255U

typedef enum {
    GIMBAL_CMD_INVALID = 0,
    GIMBAL_CMD_PING,
    GIMBAL_CMD_VER,
    GIMBAL_CMD_HELLO,
    GIMBAL_CMD_ARM,
    GIMBAL_CMD_ARM_MAINTENANCE,
    GIMBAL_CMD_STOP,
    GIMBAL_CMD_HOLD_NOW,
    GIMBAL_CMD_TRACK_YP,
    GIMBAL_CMD_POSE,
    GIMBAL_CMD_FAULT,
    GIMBAL_CMD_FAULT_CLEAR,
    GIMBAL_CMD_CAL_ELECTRICAL,
    GIMBAL_CMD_ORIGIN_CAPTURE,
    GIMBAL_CMD_NVM_STATUS,
    GIMBAL_CMD_NVM_CLEAR_ORIGIN,
    GIMBAL_CMD_NVM_CLEAR_ALL,
    GIMBAL_CMD_CURRENT,
    GIMBAL_CMD_MAINTASSIST,
    GIMBAL_CMD_CURRENT_RESET,
    GIMBAL_CMD_PROTECT_CURRENT_SET,
    GIMBAL_CMD_PROTECT_CURRENT_CLEAR
} GimbalCommandType;

typedef enum {
    GIMBAL_PROTOCOL_AXIS_P = 0,
    GIMBAL_PROTOCOL_AXIS_Y,
    GIMBAL_PROTOCOL_AXIS_ALL
} GimbalProtocolAxis;

typedef enum {
    GIMBAL_STATE_DISABLED = 0,
    GIMBAL_STATE_ARMING,
    GIMBAL_STATE_READY,
    GIMBAL_STATE_HOLDING,
    GIMBAL_STATE_MOVING,
    GIMBAL_STATE_CALIBRATING,
    GIMBAL_STATE_FAULT
} GimbalProtocolState;

typedef struct {
    GimbalCommandType type;
    union {
        struct { uint16_t yaw10; uint16_t pitch10; } track;
        struct { GimbalProtocolAxis axis; } calibration;
        struct {
            uint16_t p_peak;
            uint16_t p_average;
            uint16_t y_peak;
            uint16_t y_average;
            uint16_t stall_ms;
        } protection;
    } argument;
} GimbalProtocolCommand;

typedef enum {
    GIMBAL_PARSE_OK = 0,
    GIMBAL_PARSE_UNKNOWN_COMMAND,
    GIMBAL_PARSE_BAD_FORMAT,
    GIMBAL_PARSE_BAD_RANGE
} GimbalParseResult;

GimbalParseResult gimbal_protocol_parse(const char *line, size_t length,
                                        GimbalProtocolCommand *command);
bool gimbal_protocol_changes_state(GimbalCommandType command);
bool gimbal_protocol_command_allowed(GimbalProtocolState state,
                                     GimbalCommandType command);
const char *gimbal_protocol_state_name(GimbalProtocolState state);

#ifdef __cplusplus
}
#endif

#endif
