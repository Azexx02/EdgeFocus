#include "gimbal_protocol.h"

#include <string.h>

static bool exact(const char *line, size_t length, const char *literal) {
    const size_t expected = strlen(literal);
    return length == expected && memcmp(line, literal, expected) == 0;
}

static bool parse_decimal(const char **cursor, const char *end,
                          uint32_t maximum, uint16_t *value) {
    uint32_t parsed = 0U;
    const char *start = *cursor;
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        parsed = parsed * 10U + (uint32_t)(**cursor - '0');
        if (parsed > maximum) return false;
        ++*cursor;
    }
    if (*cursor == start) return false;
    *value = (uint16_t)parsed;
    return true;
}

static GimbalParseResult parse_track(const char *line, size_t length,
                                     GimbalProtocolCommand *command) {
    const char prefix[] = "TRACK YP ";
    const char *cursor = line + sizeof(prefix) - 1U;
    const char *end = line + length;
    if (!parse_decimal(&cursor, end, 3599U, &command->argument.track.yaw10)) {
        return GIMBAL_PARSE_BAD_RANGE;
    }
    if (cursor == end || *cursor++ != ' ') return GIMBAL_PARSE_BAD_FORMAT;
    if (!parse_decimal(&cursor, end, 3599U,
                       &command->argument.track.pitch10)) {
        return GIMBAL_PARSE_BAD_RANGE;
    }
    if (cursor != end) return GIMBAL_PARSE_BAD_FORMAT;
    command->type = GIMBAL_CMD_TRACK_YP;
    return GIMBAL_PARSE_OK;
}

static bool consume(const char **cursor, const char *end,
                    const char *literal) {
    const size_t count = strlen(literal);
    if ((size_t)(end - *cursor) < count || memcmp(*cursor, literal, count) != 0) {
        return false;
    }
    *cursor += count;
    return true;
}

static GimbalParseResult parse_protection(const char *line, size_t length,
                                          GimbalProtocolCommand *command) {
    const char prefix[] = "PROTECT CURRENT SET ";
    const char *cursor = line + sizeof(prefix) - 1U;
    const char *end = line + length;
    uint16_t *values[] = {
        &command->argument.protection.p_peak,
        &command->argument.protection.p_average,
        &command->argument.protection.y_peak,
        &command->argument.protection.y_average,
        &command->argument.protection.stall_ms
    };
    const char *labels[] = {
        "P_PEAK=", "P_AVG=", "Y_PEAK=", "Y_AVG=", "STALL_MS="
    };
    const uint16_t maxima[] = {4095U, 4095U, 4095U, 4095U, 10000U};
    unsigned index;
    for (index = 0U; index < 5U; ++index) {
        if (!consume(&cursor, end, labels[index])) return GIMBAL_PARSE_BAD_FORMAT;
        if (!parse_decimal(&cursor, end, maxima[index], values[index])) {
            return GIMBAL_PARSE_BAD_RANGE;
        }
        if (index != 4U && (cursor == end || *cursor++ != ' ')) {
            return GIMBAL_PARSE_BAD_FORMAT;
        }
    }
    if (cursor != end) return GIMBAL_PARSE_BAD_FORMAT;
    if (command->argument.protection.p_average == 0U ||
        command->argument.protection.y_average == 0U ||
        command->argument.protection.p_peak <= command->argument.protection.p_average ||
        command->argument.protection.y_peak <= command->argument.protection.y_average ||
        command->argument.protection.stall_ms < 10U) {
        return GIMBAL_PARSE_BAD_RANGE;
    }
    command->type = GIMBAL_CMD_PROTECT_CURRENT_SET;
    return GIMBAL_PARSE_OK;
}

GimbalParseResult gimbal_protocol_parse(const char *line, size_t length,
                                        GimbalProtocolCommand *command) {
    if (line == NULL || command == NULL || length == 0U ||
        length > GIMBAL_PROTOCOL_MAX_LINE) return GIMBAL_PARSE_BAD_FORMAT;
    memset(command, 0, sizeof(*command));
#define MATCH(text, value) do { if (exact(line, length, text)) { command->type = value; return GIMBAL_PARSE_OK; } } while (0)
    MATCH("PING", GIMBAL_CMD_PING);
    MATCH("VER", GIMBAL_CMD_VER);
    MATCH("HELLO HOST PROTO=3.0", GIMBAL_CMD_HELLO);
    MATCH("ARM", GIMBAL_CMD_ARM);
    MATCH("ARM MAINTENANCE", GIMBAL_CMD_ARM_MAINTENANCE);
    MATCH("STOP", GIMBAL_CMD_STOP);
    MATCH("HOLD NOW", GIMBAL_CMD_HOLD_NOW);
    MATCH("POSE", GIMBAL_CMD_POSE);
    MATCH("FAULT", GIMBAL_CMD_FAULT);
    MATCH("FAULT CLEAR", GIMBAL_CMD_FAULT_CLEAR);
    MATCH("ORIGIN CAPTURE", GIMBAL_CMD_ORIGIN_CAPTURE);
    MATCH("NVM STATUS", GIMBAL_CMD_NVM_STATUS);
    MATCH("NVM CLEAR ORIGIN", GIMBAL_CMD_NVM_CLEAR_ORIGIN);
    MATCH("NVM CLEAR ALL", GIMBAL_CMD_NVM_CLEAR_ALL);
    MATCH("CURRENT", GIMBAL_CMD_CURRENT);
    MATCH("CURRENT RESET", GIMBAL_CMD_CURRENT_RESET);
    MATCH("PROTECT CURRENT CLEAR", GIMBAL_CMD_PROTECT_CURRENT_CLEAR);
#undef MATCH
    if (length >= 9U && memcmp(line, "TRACK YP ", 9U) == 0) {
        return parse_track(line, length, command);
    }
    if (length >= 15U && memcmp(line, "CAL ELECTRICAL ", 15U) == 0) {
        command->type = GIMBAL_CMD_CAL_ELECTRICAL;
        if (exact(line + 15U, length - 15U, "P")) {
            command->argument.calibration.axis = GIMBAL_PROTOCOL_AXIS_P;
        } else if (exact(line + 15U, length - 15U, "Y")) {
            command->argument.calibration.axis = GIMBAL_PROTOCOL_AXIS_Y;
        } else if (exact(line + 15U, length - 15U, "ALL")) {
            command->argument.calibration.axis = GIMBAL_PROTOCOL_AXIS_ALL;
        } else {
            return GIMBAL_PARSE_BAD_FORMAT;
        }
        return GIMBAL_PARSE_OK;
    }
    if (length >= 20U && memcmp(line, "PROTECT CURRENT SET ", 20U) == 0) {
        return parse_protection(line, length, command);
    }
    return GIMBAL_PARSE_UNKNOWN_COMMAND;
}

bool gimbal_protocol_changes_state(GimbalCommandType command) {
    return command == GIMBAL_CMD_HELLO || command == GIMBAL_CMD_ARM ||
           command == GIMBAL_CMD_ARM_MAINTENANCE || command == GIMBAL_CMD_STOP ||
           command == GIMBAL_CMD_HOLD_NOW || command == GIMBAL_CMD_TRACK_YP ||
           command == GIMBAL_CMD_FAULT_CLEAR || command == GIMBAL_CMD_CAL_ELECTRICAL ||
           command == GIMBAL_CMD_ORIGIN_CAPTURE ||
           command == GIMBAL_CMD_NVM_CLEAR_ORIGIN || command == GIMBAL_CMD_NVM_CLEAR_ALL ||
           command == GIMBAL_CMD_CURRENT_RESET ||
           command == GIMBAL_CMD_PROTECT_CURRENT_SET ||
           command == GIMBAL_CMD_PROTECT_CURRENT_CLEAR;
}

bool gimbal_protocol_command_allowed(GimbalProtocolState state,
                                     GimbalCommandType command) {
    switch (command) {
    case GIMBAL_CMD_PING: case GIMBAL_CMD_VER: case GIMBAL_CMD_HELLO:
    case GIMBAL_CMD_POSE: case GIMBAL_CMD_FAULT: case GIMBAL_CMD_NVM_STATUS:
    case GIMBAL_CMD_CURRENT:
        return true;
    case GIMBAL_CMD_ARM: case GIMBAL_CMD_ARM_MAINTENANCE:
    case GIMBAL_CMD_CAL_ELECTRICAL: case GIMBAL_CMD_NVM_CLEAR_ORIGIN:
    case GIMBAL_CMD_NVM_CLEAR_ALL: case GIMBAL_CMD_PROTECT_CURRENT_SET:
    case GIMBAL_CMD_PROTECT_CURRENT_CLEAR:
        return state == GIMBAL_STATE_DISABLED;
    case GIMBAL_CMD_STOP:
        return state != GIMBAL_STATE_DISABLED;
    case GIMBAL_CMD_HOLD_NOW:
        return state == GIMBAL_STATE_READY || state == GIMBAL_STATE_HOLDING ||
               state == GIMBAL_STATE_MOVING;
    case GIMBAL_CMD_TRACK_YP:
        return state == GIMBAL_STATE_HOLDING || state == GIMBAL_STATE_MOVING;
    case GIMBAL_CMD_ORIGIN_CAPTURE:
        return state == GIMBAL_STATE_DISABLED || state == GIMBAL_STATE_READY ||
               state == GIMBAL_STATE_HOLDING;
    case GIMBAL_CMD_CURRENT_RESET:
        return state == GIMBAL_STATE_READY || state == GIMBAL_STATE_HOLDING;
    case GIMBAL_CMD_FAULT_CLEAR:
        return state == GIMBAL_STATE_FAULT;
    default:
        return false;
    }
}

const char *gimbal_protocol_state_name(GimbalProtocolState state) {
    static const char *const names[] = {
        "DISABLED", "ARMING", "READY", "HOLDING", "MOVING",
        "CALIBRATING", "FAULT"
    };
    return state <= GIMBAL_STATE_FAULT ? names[state] : "UNKNOWN";
}
