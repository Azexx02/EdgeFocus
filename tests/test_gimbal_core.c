#include "gimbal_angle.h"
#include "gimbal_nv.h"

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

int main(void) {
    test_angles();
    test_encoding_and_selection();
    test_atomic_store_and_tombstones();
    puts("gimbal core tests passed");
    return 0;
}
