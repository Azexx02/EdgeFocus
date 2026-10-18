#ifndef GIMBAL_NV_STM32_H
#define GIMBAL_NV_STM32_H

#include "gimbal_nv.h"

typedef struct {
    GimbalNvRecordV3 record;
    GimbalNvSlotStatus slot_status[GIMBAL_NV_SLOT_COUNT];
    int8_t active_slot;
    bool has_valid_record;
    bool maintenance_required;
} GimbalNvStorage;

void gimbal_nv_storage_init(GimbalNvStorage *storage);
GimbalNvStoreResult gimbal_nv_storage_commit(
    GimbalNvStorage *storage, const GimbalNvRecordV3 *desired_record);

#endif
