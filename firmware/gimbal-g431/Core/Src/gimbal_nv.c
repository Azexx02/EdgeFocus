#include "gimbal_nv.h"

#include <string.h>

enum {
    OFFSET_MAGIC = 0,
    OFFSET_SCHEMA = 4,
    OFFSET_LENGTH = 6,
    OFFSET_GENERATION = 8,
    OFFSET_AXIS = 12,
    OFFSET_ORIGIN = 24,
    OFFSET_CAL_EPOCH = 28,
    OFFSET_ORIGIN_EPOCH = 32,
    OFFSET_CURRENT = 36,
    OFFSET_SAFE_MIN = 46,
    OFFSET_SAFE_MAX = 50,
    OFFSET_VALID_FLAGS = 54,
    OFFSET_CRC = 60
};

static uint16_t read_u16(const uint8_t *input) {
    return (uint16_t)input[0] | ((uint16_t)input[1] << 8);
}

static int16_t read_i16(const uint8_t *input) {
    return (int16_t)read_u16(input);
}

static uint32_t read_u32(const uint8_t *input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
           ((uint32_t)input[2] << 16) | ((uint32_t)input[3] << 24);
}

static void write_u16(uint8_t *output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void write_i16(uint8_t *output, int16_t value) {
    write_u16(output, (uint16_t)value);
}

static void write_u32(uint8_t *output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static bool erased_record(const uint8_t *input) {
    size_t index;
    for (index = 0; index < GIMBAL_NV_RECORD_LENGTH; ++index) {
        if (input[index] != 0xFFU) return false;
    }
    return true;
}

void gimbal_nv_record_defaults(GimbalNvRecordV3 *record) {
    if (record == NULL) return;
    memset(record, 0, sizeof(*record));
    record->safe_min_tenths[0] = -700;
    record->safe_min_tenths[1] = -700;
    record->safe_max_tenths[0] = 700;
    record->safe_max_tenths[1] = 700;
}

bool gimbal_nv_record_valid(const GimbalNvRecordV3 *record) {
    unsigned axis;
    const uint32_t known_flags = GIMBAL_NV_VALID_ELECTRICAL |
                                 GIMBAL_NV_VALID_ORIGIN |
                                 GIMBAL_NV_VALID_CURRENT_PROTECTION;
    if (record == NULL || (record->valid_flags & ~known_flags) != 0U) return false;
    for (axis = 0; axis < 2; ++axis) {
        if (record->safe_min_tenths[axis] > 0 ||
            record->safe_max_tenths[axis] < 0 ||
            record->safe_min_tenths[axis] >= record->safe_max_tenths[axis] ||
            record->safe_min_tenths[axis] < -1800 ||
            record->safe_max_tenths[axis] > 1799) {
            return false;
        }
        if ((record->valid_flags & GIMBAL_NV_VALID_ORIGIN) != 0U &&
            record->origin_raw[axis] >= 4096U) {
            return false;
        }
        if ((record->valid_flags & GIMBAL_NV_VALID_ELECTRICAL) != 0U &&
            ((record->axis[axis].raw_direction != 1 &&
              record->axis[axis].raw_direction != -1) ||
             (record->axis[axis].torque_polarity != 1 &&
              record->axis[axis].torque_polarity != -1) ||
             record->axis[axis].pole_pairs == 0U ||
             record->axis[axis].pole_pairs > 32U ||
             record->axis[axis].electrical_offset_raw >= 4096U)) {
            return false;
        }
        if ((record->valid_flags & GIMBAL_NV_VALID_CURRENT_PROTECTION) != 0U &&
            (record->current_protection.average_limit_raw[axis] == 0U ||
             record->current_protection.peak_limit_raw[axis] <=
                 record->current_protection.average_limit_raw[axis] ||
             record->current_protection.peak_limit_raw[axis] > 4095U)) {
            return false;
        }
    }
    if ((record->valid_flags & GIMBAL_NV_VALID_CURRENT_PROTECTION) != 0U &&
        (record->current_protection.stall_ms < 10U ||
         record->current_protection.stall_ms > 10000U)) {
        return false;
    }
    return true;
}

uint32_t gimbal_nv_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    size_t index;
    unsigned bit;
    if (data == NULL) return 0U;
    for (index = 0; index < length; ++index) {
        crc ^= data[index];
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

bool gimbal_nv_generation_newer(uint32_t candidate, uint32_t reference) {
    return candidate != reference &&
           (uint32_t)(candidate - reference) < 0x80000000UL;
}

bool gimbal_nv_encode(const GimbalNvRecordV3 *record,
                      uint8_t output[GIMBAL_NV_RECORD_LENGTH]) {
    unsigned axis;
    if (record == NULL || output == NULL || !gimbal_nv_record_valid(record)) {
        return false;
    }
    memset(output, 0, GIMBAL_NV_RECORD_LENGTH);
    write_u32(output + OFFSET_MAGIC, GIMBAL_NV_MAGIC);
    write_u16(output + OFFSET_SCHEMA, GIMBAL_NV_SCHEMA_VERSION);
    write_u16(output + OFFSET_LENGTH, GIMBAL_NV_RECORD_LENGTH);
    write_u32(output + OFFSET_GENERATION, record->generation);
    for (axis = 0; axis < 2; ++axis) {
        const size_t offset = OFFSET_AXIS + axis * 6U;
        output[offset] = (uint8_t)record->axis[axis].raw_direction;
        output[offset + 1U] = (uint8_t)record->axis[axis].torque_polarity;
        output[offset + 2U] = record->axis[axis].pole_pairs;
        output[offset + 3U] = 0U;
        write_u16(output + offset + 4U, record->axis[axis].electrical_offset_raw);
        write_u16(output + OFFSET_ORIGIN + axis * 2U, record->origin_raw[axis]);
        write_u16(output + OFFSET_CURRENT + axis * 4U,
                  record->current_protection.peak_limit_raw[axis]);
        write_u16(output + OFFSET_CURRENT + axis * 4U + 2U,
                  record->current_protection.average_limit_raw[axis]);
        write_i16(output + OFFSET_SAFE_MIN + axis * 2U,
                  record->safe_min_tenths[axis]);
        write_i16(output + OFFSET_SAFE_MAX + axis * 2U,
                  record->safe_max_tenths[axis]);
    }
    write_u32(output + OFFSET_CAL_EPOCH, record->calibration_epoch);
    write_u32(output + OFFSET_ORIGIN_EPOCH, record->origin_epoch);
    write_u16(output + OFFSET_CURRENT + 8U,
              record->current_protection.stall_ms);
    write_u32(output + OFFSET_VALID_FLAGS, record->valid_flags);
    write_u32(output + OFFSET_CRC, gimbal_nv_crc32(output, OFFSET_CRC));
    return true;
}

GimbalNvSlotStatus gimbal_nv_decode(
    const uint8_t input[GIMBAL_NV_RECORD_LENGTH], GimbalNvRecordV3 *record) {
    unsigned axis;
    GimbalNvRecordV3 decoded;
    if (input == NULL) return GIMBAL_NV_SLOT_CORRUPT;
    if (erased_record(input)) return GIMBAL_NV_SLOT_EMPTY;
    if (read_u32(input + OFFSET_MAGIC) != GIMBAL_NV_MAGIC) {
        return GIMBAL_NV_SLOT_CORRUPT;
    }
    if (read_u16(input + OFFSET_SCHEMA) != GIMBAL_NV_SCHEMA_VERSION) {
        return GIMBAL_NV_SLOT_UNSUPPORTED_SCHEMA;
    }
    if (read_u16(input + OFFSET_LENGTH) != GIMBAL_NV_RECORD_LENGTH ||
        read_u32(input + OFFSET_CRC) != gimbal_nv_crc32(input, OFFSET_CRC)) {
        return GIMBAL_NV_SLOT_CORRUPT;
    }
    memset(&decoded, 0, sizeof(decoded));
    decoded.generation = read_u32(input + OFFSET_GENERATION);
    for (axis = 0; axis < 2; ++axis) {
        const size_t offset = OFFSET_AXIS + axis * 6U;
        decoded.axis[axis].raw_direction = (int8_t)input[offset];
        decoded.axis[axis].torque_polarity = (int8_t)input[offset + 1U];
        decoded.axis[axis].pole_pairs = input[offset + 2U];
        decoded.axis[axis].electrical_offset_raw = read_u16(input + offset + 4U);
        decoded.origin_raw[axis] = read_u16(input + OFFSET_ORIGIN + axis * 2U);
        decoded.current_protection.peak_limit_raw[axis] =
            read_u16(input + OFFSET_CURRENT + axis * 4U);
        decoded.current_protection.average_limit_raw[axis] =
            read_u16(input + OFFSET_CURRENT + axis * 4U + 2U);
        decoded.safe_min_tenths[axis] =
            read_i16(input + OFFSET_SAFE_MIN + axis * 2U);
        decoded.safe_max_tenths[axis] =
            read_i16(input + OFFSET_SAFE_MAX + axis * 2U);
    }
    decoded.calibration_epoch = read_u32(input + OFFSET_CAL_EPOCH);
    decoded.origin_epoch = read_u32(input + OFFSET_ORIGIN_EPOCH);
    decoded.current_protection.stall_ms = read_u16(input + OFFSET_CURRENT + 8U);
    decoded.valid_flags = read_u32(input + OFFSET_VALID_FLAGS);
    if (!gimbal_nv_record_valid(&decoded)) return GIMBAL_NV_SLOT_CORRUPT;
    if (record != NULL) *record = decoded;
    return GIMBAL_NV_SLOT_VALID;
}

bool gimbal_nv_select(const uint8_t slots[GIMBAL_NV_SLOT_COUNT]
                                         [GIMBAL_NV_RECORD_LENGTH],
                      GimbalNvRecordV3 *record, int8_t *active_slot,
                      bool *maintenance_required,
                      GimbalNvSlotStatus status[GIMBAL_NV_SLOT_COUNT]) {
    GimbalNvRecordV3 decoded[2];
    int8_t selected = -1;
    unsigned slot;
    if (slots == NULL) return false;
    for (slot = 0; slot < GIMBAL_NV_SLOT_COUNT; ++slot) {
        const GimbalNvSlotStatus current = gimbal_nv_decode(slots[slot], &decoded[slot]);
        if (status != NULL) status[slot] = current;
        if (current != GIMBAL_NV_SLOT_VALID) continue;
        if (selected < 0 || gimbal_nv_generation_newer(
                                decoded[slot].generation,
                                decoded[(unsigned)selected].generation)) {
            selected = (int8_t)slot;
        }
    }
    if (active_slot != NULL) *active_slot = selected;
    if (maintenance_required != NULL) {
        *maintenance_required = selected < 0;
    }
    if (selected < 0) return false;
    if (record != NULL) *record = decoded[(unsigned)selected];
    return true;
}

GimbalNvStoreResult gimbal_nv_store(const GimbalNvIo *io,
                                    const GimbalNvRecordV3 *active_record,
                                    int8_t active_slot,
                                    const GimbalNvRecordV3 *desired_record,
                                    GimbalNvRecordV3 *committed_record,
                                    int8_t *committed_slot) {
    uint8_t encoded[GIMBAL_NV_RECORD_LENGTH];
    uint8_t verify[GIMBAL_NV_RECORD_LENGTH];
    GimbalNvRecordV3 next;
    GimbalNvRecordV3 decoded;
    const uint8_t destination = active_slot == 0 ? 1U : 0U;
    if (io == NULL || io->read == NULL || io->erase == NULL ||
        io->program == NULL || desired_record == NULL ||
        active_slot < -1 || active_slot > 1) {
        return GIMBAL_NV_STORE_BAD_ARGUMENT;
    }
    next = *desired_record;
    next.generation = active_record == NULL ? 1U : active_record->generation + 1U;
    if (!gimbal_nv_encode(&next, encoded)) return GIMBAL_NV_STORE_INVALID_RECORD;
    if (!io->erase(io->context, destination)) return GIMBAL_NV_STORE_ERASE_FAILED;
    if (!io->program(io->context, destination, encoded, sizeof(encoded))) {
        return GIMBAL_NV_STORE_PROGRAM_FAILED;
    }
    if (!io->read(io->context, destination, verify, sizeof(verify))) {
        return GIMBAL_NV_STORE_READ_FAILED;
    }
    if (memcmp(encoded, verify, sizeof(encoded)) != 0 ||
        gimbal_nv_decode(verify, &decoded) != GIMBAL_NV_SLOT_VALID ||
        decoded.generation != next.generation) {
        return GIMBAL_NV_STORE_VERIFY_FAILED;
    }
    if (committed_record != NULL) *committed_record = decoded;
    if (committed_slot != NULL) *committed_slot = (int8_t)destination;
    return GIMBAL_NV_STORE_OK;
}

void gimbal_nv_clear_origin(GimbalNvRecordV3 *record) {
    if (record == NULL) return;
    record->valid_flags &= ~GIMBAL_NV_VALID_ORIGIN;
    record->origin_raw[0] = 0U;
    record->origin_raw[1] = 0U;
    ++record->origin_epoch;
}

void gimbal_nv_clear_all(GimbalNvRecordV3 *record) {
    if (record == NULL) return;
    memset(record->axis, 0, sizeof(record->axis));
    memset(record->origin_raw, 0, sizeof(record->origin_raw));
    memset(&record->current_protection, 0, sizeof(record->current_protection));
    record->valid_flags &= ~(GIMBAL_NV_VALID_ELECTRICAL |
                             GIMBAL_NV_VALID_ORIGIN |
                             GIMBAL_NV_VALID_CURRENT_PROTECTION);
    ++record->calibration_epoch;
    ++record->origin_epoch;
}
