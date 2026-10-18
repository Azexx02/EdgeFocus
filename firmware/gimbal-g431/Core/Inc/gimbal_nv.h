#ifndef GIMBAL_NV_H
#define GIMBAL_NV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIMBAL_NV_MAGIC 0x33564247UL
#define GIMBAL_NV_SCHEMA_VERSION 3U
#define GIMBAL_NV_RECORD_LENGTH 64U
#define GIMBAL_NV_SLOT_COUNT 2U
#define GIMBAL_NV_SLOT_SIZE 2048U

#define GIMBAL_NV_VALID_ELECTRICAL (1UL << 0)
#define GIMBAL_NV_VALID_ORIGIN (1UL << 1)
#define GIMBAL_NV_VALID_CURRENT_PROTECTION (1UL << 2)

typedef struct {
    int8_t raw_direction;
    int8_t torque_polarity;
    uint8_t pole_pairs;
    uint16_t electrical_offset_raw;
} GimbalAxisElectricalCalibration;

typedef struct {
    uint16_t peak_limit_raw[2];
    uint16_t average_limit_raw[2];
    uint16_t stall_ms;
} GimbalCurrentProtectionConfig;

typedef struct {
    uint32_t generation;
    GimbalAxisElectricalCalibration axis[2];
    uint16_t origin_raw[2];
    uint32_t calibration_epoch;
    uint32_t origin_epoch;
    GimbalCurrentProtectionConfig current_protection;
    int16_t safe_min_tenths[2];
    int16_t safe_max_tenths[2];
    uint32_t valid_flags;
} GimbalNvRecordV3;

typedef enum {
    GIMBAL_NV_SLOT_EMPTY = 0,
    GIMBAL_NV_SLOT_VALID,
    GIMBAL_NV_SLOT_UNSUPPORTED_SCHEMA,
    GIMBAL_NV_SLOT_CORRUPT
} GimbalNvSlotStatus;

typedef enum {
    GIMBAL_NV_STORE_OK = 0,
    GIMBAL_NV_STORE_BAD_ARGUMENT,
    GIMBAL_NV_STORE_INVALID_RECORD,
    GIMBAL_NV_STORE_ERASE_FAILED,
    GIMBAL_NV_STORE_PROGRAM_FAILED,
    GIMBAL_NV_STORE_READ_FAILED,
    GIMBAL_NV_STORE_VERIFY_FAILED
} GimbalNvStoreResult;

typedef struct {
    void *context;
    bool (*read)(void *context, uint8_t slot, uint8_t *data, size_t length);
    bool (*erase)(void *context, uint8_t slot);
    bool (*program)(void *context, uint8_t slot, const uint8_t *data,
                    size_t length);
} GimbalNvIo;

void gimbal_nv_record_defaults(GimbalNvRecordV3 *record);
bool gimbal_nv_record_valid(const GimbalNvRecordV3 *record);
uint32_t gimbal_nv_crc32(const uint8_t *data, size_t length);
bool gimbal_nv_generation_newer(uint32_t candidate, uint32_t reference);
bool gimbal_nv_encode(const GimbalNvRecordV3 *record,
                      uint8_t output[GIMBAL_NV_RECORD_LENGTH]);
GimbalNvSlotStatus gimbal_nv_decode(
    const uint8_t input[GIMBAL_NV_RECORD_LENGTH], GimbalNvRecordV3 *record);
bool gimbal_nv_select(const uint8_t slots[GIMBAL_NV_SLOT_COUNT]
                                         [GIMBAL_NV_RECORD_LENGTH],
                      GimbalNvRecordV3 *record, int8_t *active_slot,
                      bool *maintenance_required,
                      GimbalNvSlotStatus status[GIMBAL_NV_SLOT_COUNT]);
GimbalNvStoreResult gimbal_nv_store(const GimbalNvIo *io,
                                    const GimbalNvRecordV3 *active_record,
                                    int8_t active_slot,
                                    const GimbalNvRecordV3 *desired_record,
                                    GimbalNvRecordV3 *committed_record,
                                    int8_t *committed_slot);
void gimbal_nv_clear_origin(GimbalNvRecordV3 *record);
void gimbal_nv_clear_all(GimbalNvRecordV3 *record);

#ifdef __cplusplus
}
#endif

#endif
