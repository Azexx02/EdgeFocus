#include "gimbal_angle.h"
#include "gimbal_maintenance_stiction.h"
#include "gimbal_nv.h"
#include "gimbal_power_profile.h"
#include "gimbal_protocol.h"
#include "gimbal_rx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t slots[2][GIMBAL_NV_SLOT_SIZE];
    bool fail_erase;
    bool fail_program;
    size_t program_limit;
    bool corrupt_readback;
} FakeFlash;

static bool fake_read(void *context, uint8_t slot, uint8_t *data, size_t length) {
    FakeFlash *flash = (FakeFlash *)context;
    if (slot >= 2U || length > GIMBAL_NV_SLOT_SIZE) return false;
    memcpy(data, flash->slots[slot], length);
    if (flash->corrupt_readback && length > 16U) data[16] ^= 0x40U;
    return true;
}

static bool fake_erase(void *context, uint8_t slot) {
    FakeFlash *flash = (FakeFlash *)context;
    if (slot >= 2U || flash->fail_erase) return false;
    memset(flash->slots[slot], 0xFF, GIMBAL_NV_SLOT_SIZE);
    return true;
}

static bool fake_program(void *context, uint8_t slot, const uint8_t *data,
                         size_t length) {
    FakeFlash *flash = (FakeFlash *)context;
    size_t actual = length;
    if (slot >= 2U || length > GIMBAL_NV_SLOT_SIZE) return false;
    if (flash->program_limit != 0U && flash->program_limit < actual) {
        actual = flash->program_limit;
    }
    memcpy(flash->slots[slot], data, actual);
    return !flash->fail_program && actual == length;
}

static GimbalNvRecordV3 complete_record(void) {
    GimbalNvRecordV3 record;
    gimbal_nv_record_defaults(&record);
    record.axis[0].raw_direction = -1;
    record.axis[0].torque_polarity = 1;
    record.axis[0].pole_pairs = 7;
    record.axis[0].electrical_offset_raw = 123;
    record.axis[1].raw_direction = 1;
    record.axis[1].torque_polarity = -1;
    record.axis[1].pole_pairs = 11;
    record.axis[1].electrical_offset_raw = 3000;
    record.origin_raw[0] = 1000;
    record.origin_raw[1] = 3000;
    record.calibration_epoch = 8;
    record.origin_epoch = 4;
    record.current_protection.peak_limit_raw[0] = 1800;
    record.current_protection.average_limit_raw[0] = 1200;
    record.current_protection.peak_limit_raw[1] = 1900;
    record.current_protection.average_limit_raw[1] = 1300;
    record.current_protection.stall_ms = 500;
    record.valid_flags = GIMBAL_NV_VALID_ELECTRICAL |
                         GIMBAL_NV_VALID_ORIGIN |
                         GIMBAL_NV_VALID_CURRENT_PROTECTION;
    return record;
}

static void test_angles(void) {
    assert(gimbal_angle10_wrap(0) == 0U);
    assert(gimbal_angle10_wrap(3601) == 1U);
    assert(gimbal_angle10_wrap(-1) == 3599U);
    assert(gimbal_angle10_shortest_delta(3590U, 10U) == 20);
    assert(gimbal_angle10_shortest_delta(10U, 3590U) == -20);
    assert(gimbal_angle10_shortest_delta(0U, 1800U) == -1800);
    assert(gimbal_angle10_target_in_window(700U, -700, 700));
    assert(gimbal_angle10_target_in_window(2900U, -700, 700));
    assert(!gimbal_angle10_target_in_window(701U, -700, 700));
    assert(!gimbal_angle10_target_in_window(2899U, -700, 700));
    assert(!gimbal_angle10_target_in_window(0U, 10, 700));
    assert(gimbal_raw_to_angle10(1000U, 1000U, 1) == 0U);
    assert(gimbal_raw_to_angle10(2024U, 1000U, 1) == 900U);
    assert(gimbal_raw_to_angle10(2024U, 1000U, -1) == 2700U);
    assert(gimbal_raw_to_angle10(100U, 4000U, 1) == 172U);
}

static void test_power_profile_isolation_and_reset(void) {
    gimbal_power_profile_reset();
    assert(!gimbal_power_profile_is_maintenance());
    assert(gimbal_power_profile_hold_min(GIMBAL_POWER_AXIS_P) == 260U);
    assert(gimbal_power_profile_hold_min(GIMBAL_POWER_AXIS_Y) == 80U);
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_P) == 360U);
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_Y) == 520U);

    gimbal_power_profile_set_maintenance(true);
    assert(gimbal_power_profile_is_maintenance());
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_P) == 260U);
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_Y) == 180U);
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_P) <= 360U);
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_Y) <= 520U);

    /* STOP/output-off uses the same false transition before the next ARM. */
    gimbal_power_profile_set_maintenance(false);
    assert(!gimbal_power_profile_is_maintenance());
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_P) == 360U);
    assert(gimbal_power_profile_hold_max(GIMBAL_POWER_AXIS_Y) == 520U);
}

static GimbalMaintenanceStictionInput stalled_input(uint32_t now_ms,
                                                     int8_t direction) {
    GimbalMaintenanceStictionInput input;
    memset(&input, 0, sizeof(input));
    input.maintenance_yaw = true;
    input.target_ramp_complete = true;
    input.error_abs = 16U;
    input.speed_raw = 0;
    input.requested_magnitude = 101U;
    input.requested_direction = direction;
    input.maximum_magnitude = 180U;
    input.now_ms = now_ms;
    return input;
}

static void test_maintenance_stiction_isolation_and_direction(void) {
    GimbalMaintenanceStictionState state;
    GimbalMaintenanceStictionInput input = stalled_input(0U, 1);
    GimbalMaintenanceStictionOutput output;
    gimbal_maintenance_stiction_init(&state);
    gimbal_maintenance_stiction_new_target(&state, 7U);

    input.maintenance_yaw = false;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && output.direction == 1);
    input.maintenance_yaw = true;
    input.target_ramp_complete = false;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && output.direction == 1);
    input.target_ramp_complete = true;
    input.settled = true;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && output.direction == 1);

    gimbal_maintenance_stiction_new_target(&state, 8U);
    input = stalled_input(1000U, -1);
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && output.direction == -1);
    input.now_ms = 1149U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && output.direction == -1);
    input.now_ms = 1150U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 103U && output.direction == -1);
}

static void test_maintenance_stiction_time_bounds_and_wrap(void) {
    GimbalMaintenanceStictionState state;
    GimbalMaintenanceStictionInput input;
    GimbalMaintenanceStictionOutput output;
    GimbalMaintenanceStictionDiagnostics diagnostics;
    gimbal_maintenance_stiction_init(&state);
    gimbal_maintenance_stiction_new_target(&state, 9U);
    input = stalled_input(UINT32_MAX - 99U, 1);
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U);
    input.now_ms = 0U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U);
    input.now_ms = 50U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 103U);
    input.now_ms = 70U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 105U);
    input.now_ms = 349U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 107U);
    input.now_ms = 350U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && output.release_immediately);
    gimbal_maintenance_stiction_get_diagnostics(&state, &diagnostics);
    assert(diagnostics.target_generation == 9U);
    assert(diagnostics.attempted && diagnostics.exhausted);
    assert(diagnostics.active_ms == 300U);
    assert(diagnostics.peak_floor == 107U);

    input.now_ms = 1000U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && !state.active);
}

static void test_maintenance_stiction_reset_and_snapshot(void) {
    GimbalMaintenanceStictionState state;
    GimbalMaintenanceStictionInput input = stalled_input(100U, 1);
    GimbalMaintenanceStictionOutput output;
    GimbalMaintenanceStictionDiagnostics diagnostics;
    gimbal_maintenance_stiction_init(&state);
    gimbal_maintenance_stiction_new_target(&state, 10U);
    (void)gimbal_maintenance_stiction_update(&state, &input);
    input.now_ms = 250U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 103U);
    input.now_ms = 255U;
    input.speed_raw = 2;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && !state.active &&
           output.release_immediately);
    input.speed_raw = 0;
    input.now_ms = 1000U;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && !state.active);

    gimbal_maintenance_stiction_reset_control(&state);
    gimbal_maintenance_stiction_get_diagnostics(&state, &diagnostics);
    assert(diagnostics.target_generation == 10U);
    assert(diagnostics.attempted && diagnostics.active_ms == 5U);
    input.requested_direction = 0;
    output = gimbal_maintenance_stiction_update(&state, &input);
    assert(output.magnitude == 101U && output.direction == 0);

    gimbal_maintenance_stiction_new_target(&state, 11U);
    gimbal_maintenance_stiction_get_diagnostics(&state, &diagnostics);
    assert(diagnostics.target_generation == 11U);
    assert(!diagnostics.attempted && !diagnostics.exhausted &&
           diagnostics.active_ms == 0U && diagnostics.peak_floor == 0U);
}

static void test_encoding_and_selection(void) {
    uint8_t slots[2][GIMBAL_NV_RECORD_LENGTH];
    uint8_t encoded[GIMBAL_NV_RECORD_LENGTH];
    GimbalNvRecordV3 record = complete_record();
    GimbalNvRecordV3 decoded;
    GimbalNvSlotStatus status[2];
    int8_t active = -1;
    bool maintenance = false;
    memset(slots, 0xFF, sizeof(slots));
    record.generation = 0xFFFFFFFFUL;
    assert(gimbal_nv_encode(&record, encoded));
    assert(gimbal_nv_decode(encoded, &decoded) == GIMBAL_NV_SLOT_VALID);
    assert(decoded.generation == record.generation);
    assert(decoded.axis[0].raw_direction == -1);
    assert(decoded.axis[1].torque_polarity == -1);
    assert(decoded.safe_min_tenths[1] == -700);
    memcpy(slots[0], encoded, sizeof(encoded));
    record.generation = 0U;
    assert(gimbal_nv_encode(&record, slots[1]));
    assert(gimbal_nv_select(slots, &decoded, &active, &maintenance, status));
    assert(active == 1 && decoded.generation == 0U);
    assert(!maintenance);
    slots[1][15] ^= 1U;
    assert(gimbal_nv_select(slots, &decoded, &active, &maintenance, status));
    assert(active == 0 && decoded.generation == 0xFFFFFFFFUL);
    assert(status[1] == GIMBAL_NV_SLOT_CORRUPT);

    memcpy(slots[1], slots[0], sizeof(slots[1]));
    slots[1][4] = 2U;
    slots[1][5] = 0U;
    assert(gimbal_nv_select(slots, &decoded, &active, &maintenance, status));
    assert(active == 0 && !maintenance);
    assert(status[1] == GIMBAL_NV_SLOT_UNSUPPORTED_SCHEMA);

    memset(slots, 0xFF, sizeof(slots));
    memcpy(slots[0], encoded, sizeof(encoded));
    slots[0][4] = 2U;
    slots[0][5] = 0U;
    assert(!gimbal_nv_select(slots, NULL, &active, &maintenance, status));
    assert(active == -1 && maintenance);
    assert(status[0] == GIMBAL_NV_SLOT_UNSUPPORTED_SCHEMA);
}

static void test_atomic_store_and_tombstones(void) {
    FakeFlash flash;
    GimbalNvIo io;
    GimbalNvRecordV3 desired = complete_record();
    GimbalNvRecordV3 active;
    GimbalNvRecordV3 selected;
    GimbalNvSlotStatus statuses[2];
    uint8_t compact[2][GIMBAL_NV_RECORD_LENGTH];
    int8_t active_slot = -1;
    int8_t committed_slot = -1;
    bool maintenance = false;
    memset(&flash, 0, sizeof(flash));
    memset(flash.slots, 0xFF, sizeof(flash.slots));
    io.context = &flash;
    io.read = fake_read;
    io.erase = fake_erase;
    io.program = fake_program;

    assert(gimbal_nv_store(&io, NULL, -1, &desired, &active,
                           &active_slot) == GIMBAL_NV_STORE_OK);
    assert(active_slot == 0 && active.generation == 1U);
    desired.origin_raw[0] = 2222U;
    flash.fail_program = true;
    flash.program_limit = 24U;
    assert(gimbal_nv_store(&io, &active, active_slot, &desired, NULL,
                           NULL) == GIMBAL_NV_STORE_PROGRAM_FAILED);
    memcpy(compact[0], flash.slots[0], GIMBAL_NV_RECORD_LENGTH);
    memcpy(compact[1], flash.slots[1], GIMBAL_NV_RECORD_LENGTH);
    assert(gimbal_nv_select(compact, &selected, &committed_slot, &maintenance,
                            statuses));
    assert(committed_slot == 0 && selected.origin_raw[0] == 1000U);

    flash.fail_program = false;
    flash.program_limit = 0U;
    assert(gimbal_nv_store(&io, &active, active_slot, &desired, &active,
                           &active_slot) == GIMBAL_NV_STORE_OK);
    assert(active_slot == 1 && active.generation == 2U &&
           active.origin_raw[0] == 2222U);
    flash.corrupt_readback = true;
    assert(gimbal_nv_store(&io, &active, active_slot, &desired, NULL,
                           NULL) == GIMBAL_NV_STORE_VERIFY_FAILED);
    flash.corrupt_readback = false;

    desired = active;
    gimbal_nv_clear_origin(&desired);
    assert((desired.valid_flags & GIMBAL_NV_VALID_ORIGIN) == 0U);
    assert(desired.origin_epoch == active.origin_epoch + 1U);
    assert((desired.valid_flags & GIMBAL_NV_VALID_ELECTRICAL) != 0U);
    gimbal_nv_clear_all(&desired);
    assert((desired.valid_flags & (GIMBAL_NV_VALID_ELECTRICAL |
                                   GIMBAL_NV_VALID_ORIGIN |
                                   GIMBAL_NV_VALID_CURRENT_PROTECTION)) == 0U);
    assert(desired.calibration_epoch == active.calibration_epoch + 1U);
}

static GimbalProtocolCommand parse_ok(const char *line) {
    GimbalProtocolCommand command;
    assert(gimbal_protocol_parse(line, strlen(line), &command) ==
           GIMBAL_PARSE_OK);
    return command;
}

static void test_protocol_parser_and_state_matrix(void) {
    GimbalProtocolCommand command = parse_ok("HELLO HOST PROTO=3.0");
    assert(command.type == GIMBAL_CMD_HELLO);
    command = parse_ok("TRACK YP 3599 0");
    assert(command.type == GIMBAL_CMD_TRACK_YP);
    assert(command.argument.track.yaw10 == 3599U);
    assert(command.argument.track.pitch10 == 0U);
    command = parse_ok("CAL ELECTRICAL ALL");
    assert(command.argument.calibration.axis == GIMBAL_PROTOCOL_AXIS_ALL);
    command = parse_ok("PROTECT CURRENT SET P_PEAK=900 P_AVG=500 Y_PEAK=850 Y_AVG=450 STALL_MS=250");
    assert(command.argument.protection.p_peak == 900U);
    assert(command.argument.protection.stall_ms == 250U);
    command = parse_ok("MAINTASSIST?");
    assert(command.type == GIMBAL_CMD_MAINTASSIST);
    assert(!gimbal_protocol_changes_state(command.type));
    assert(gimbal_protocol_command_allowed(GIMBAL_STATE_DISABLED,
                                           command.type));
    assert(gimbal_protocol_command_allowed(GIMBAL_STATE_MOVING,
                                           command.type));

    assert(gimbal_protocol_parse("track YP 0 0", 12U, &command) ==
           GIMBAL_PARSE_UNKNOWN_COMMAND);
    assert(gimbal_protocol_parse("TRACK YP 3600 0", 15U, &command) ==
           GIMBAL_PARSE_BAD_RANGE);
    assert(gimbal_protocol_parse("TRACK YP 1 2 ", 13U, &command) ==
           GIMBAL_PARSE_BAD_FORMAT);
    assert(gimbal_protocol_parse("HELLO HOST PROTO=2.0", 20U, &command) ==
           GIMBAL_PARSE_UNKNOWN_COMMAND);
    {
        const char invalid_protection[] =
            "PROTECT CURRENT SET P_PEAK=500 P_AVG=500 Y_PEAK=850 Y_AVG=450 STALL_MS=250";
        assert(gimbal_protocol_parse(invalid_protection,
                                     strlen(invalid_protection), &command) ==
               GIMBAL_PARSE_BAD_RANGE);
    }

    assert(gimbal_protocol_command_allowed(GIMBAL_STATE_DISABLED,
                                           GIMBAL_CMD_ARM));
    assert(!gimbal_protocol_command_allowed(GIMBAL_STATE_READY,
                                            GIMBAL_CMD_ARM));
    assert(gimbal_protocol_command_allowed(GIMBAL_STATE_READY,
                                           GIMBAL_CMD_HOLD_NOW));
    assert(!gimbal_protocol_command_allowed(GIMBAL_STATE_READY,
                                            GIMBAL_CMD_TRACK_YP));
    assert(gimbal_protocol_command_allowed(GIMBAL_STATE_MOVING,
                                           GIMBAL_CMD_TRACK_YP));
    assert(gimbal_protocol_command_allowed(GIMBAL_STATE_FAULT,
                                           GIMBAL_CMD_FAULT_CLEAR));
    assert(!gimbal_protocol_command_allowed(GIMBAL_STATE_FAULT,
                                            GIMBAL_CMD_ORIGIN_CAPTURE));
}

static void test_rx_fragmentation_sticky_lines_and_emergency(void) {
    GimbalRxFramer framer;
    const char *line = NULL;
    size_t length = 0U;
    GimbalRxEvent event = GIMBAL_RX_NONE;
    const char fragmented[] = "HELLO HOST PROTO=3.0\r";
    size_t index;
    gimbal_rx_init(&framer);
    for (index = 0U; index < sizeof(fragmented) - 1U; ++index) {
        event = gimbal_rx_feed(&framer, (uint8_t)fragmented[index],
                               &line, &length);
    }
    assert(event == GIMBAL_RX_LINE);
    assert(length == strlen("HELLO HOST PROTO=3.0"));
    assert(memcmp(line, "HELLO HOST PROTO=3.0", length) == 0);
    assert(gimbal_rx_feed(&framer, '\n', &line, &length) == GIMBAL_RX_NONE);

    {
        const char sticky[] = "PING\r\nVER\n";
        unsigned line_count = 0U;
        for (index = 0U; index < sizeof(sticky) - 1U; ++index) {
            event = gimbal_rx_feed(&framer, (uint8_t)sticky[index],
                                   &line, &length);
            if (event == GIMBAL_RX_LINE) ++line_count;
        }
        assert(line_count == 2U);
    }

    {
        const char partial[] = "TRACK YP 10";
        for (index = 0U; index < sizeof(partial) - 1U; ++index) {
            assert(gimbal_rx_feed(&framer, (uint8_t)partial[index],
                                  &line, &length) == GIMBAL_RX_NONE);
        }
        assert(gimbal_rx_feed(&framer, '!', &line, &length) ==
               GIMBAL_RX_EMERGENCY);
        assert(gimbal_rx_feed(&framer, '\r', &line, &length) == GIMBAL_RX_NONE);
    }

    for (index = 0U; index < GIMBAL_PROTOCOL_MAX_LINE; ++index) {
        assert(gimbal_rx_feed(&framer, 'A', &line, &length) == GIMBAL_RX_NONE);
    }
    assert(gimbal_rx_feed(&framer, 'A', &line, &length) ==
           GIMBAL_RX_BAD_FORMAT);
    assert(gimbal_rx_feed(&framer, '\r', &line, &length) == GIMBAL_RX_NONE);
    assert(gimbal_rx_feed(&framer, 'P', &line, &length) == GIMBAL_RX_NONE);
}

int main(void) {
    test_angles();
    test_power_profile_isolation_and_reset();
    test_maintenance_stiction_isolation_and_direction();
    test_maintenance_stiction_time_bounds_and_wrap();
    test_maintenance_stiction_reset_and_snapshot();
    test_encoding_and_selection();
    test_atomic_store_and_tombstones();
    test_protocol_parser_and_state_matrix();
    test_rx_fragmentation_sticky_lines_and_emergency();
    puts("gimbal core tests passed");
    return 0;
}
