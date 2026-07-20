#include "gimbal_nv_stm32.h"

#include "main.h"

#include <stdint.h>
#include <string.h>

#define GIMBAL_NV_SLOT_A_ADDRESS 0x0801F000UL
#define GIMBAL_NV_SLOT_B_ADDRESS 0x0801F800UL
#define GIMBAL_NV_FLASH_BASE 0x08000000UL
#define GIMBAL_NV_FLASH_PAGE_SIZE 2048UL

static uint32_t slot_address(uint8_t slot) {
    return slot == 0U ? GIMBAL_NV_SLOT_A_ADDRESS : GIMBAL_NV_SLOT_B_ADDRESS;
}

static bool stm32_read(void *context, uint8_t slot, uint8_t *data,
                       size_t length) {
    (void)context;
    if (slot >= GIMBAL_NV_SLOT_COUNT || data == NULL ||
        length > GIMBAL_NV_SLOT_SIZE) {
        return false;
    }
    memcpy(data, (const void *)(uintptr_t)slot_address(slot), length);
    return true;
}

static bool stm32_erase(void *context, uint8_t slot) {
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0U;
    HAL_StatusTypeDef result;
    (void)context;
    if (slot >= GIMBAL_NV_SLOT_COUNT) return false;
    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks = FLASH_BANK_1;
    erase.Page = (slot_address(slot) - GIMBAL_NV_FLASH_BASE) /
                 GIMBAL_NV_FLASH_PAGE_SIZE;
    erase.NbPages = 1U;
    if (HAL_FLASH_Unlock() != HAL_OK) return false;
    result = HAL_FLASHEx_Erase(&erase, &page_error);
    (void)HAL_FLASH_Lock();
    return result == HAL_OK && page_error == 0xFFFFFFFFUL;
}

static bool stm32_program(void *context, uint8_t slot, const uint8_t *data,
                          size_t length) {
    uint32_t address;
    size_t offset;
    bool ok = true;
    (void)context;
    if (slot >= GIMBAL_NV_SLOT_COUNT || data == NULL ||
        length == 0U || length > GIMBAL_NV_SLOT_SIZE ||
        (length % sizeof(uint64_t)) != 0U) {
        return false;
    }
    address = slot_address(slot);
    if (HAL_FLASH_Unlock() != HAL_OK) return false;
    for (offset = 0U; offset < length; offset += sizeof(uint64_t)) {
        uint64_t double_word;
        memcpy(&double_word, data + offset, sizeof(double_word));
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              address + (uint32_t)offset,
                              double_word) != HAL_OK) {
            ok = false;
            break;
        }
    }
    (void)HAL_FLASH_Lock();
    return ok;
}

void gimbal_nv_storage_init(GimbalNvStorage *storage) {
    uint8_t slots[GIMBAL_NV_SLOT_COUNT][GIMBAL_NV_RECORD_LENGTH];
    if (storage == NULL) return;
    memset(storage, 0, sizeof(*storage));
    storage->active_slot = -1;
    (void)stm32_read(NULL, 0U, slots[0], GIMBAL_NV_RECORD_LENGTH);
    (void)stm32_read(NULL, 1U, slots[1], GIMBAL_NV_RECORD_LENGTH);
    storage->has_valid_record = gimbal_nv_select(
        slots, &storage->record, &storage->active_slot,
        &storage->maintenance_required, storage->slot_status);
    if (!storage->has_valid_record) {
        gimbal_nv_record_defaults(&storage->record);
        storage->maintenance_required = true;
    }
}

GimbalNvStoreResult gimbal_nv_storage_commit(
    GimbalNvStorage *storage, const GimbalNvRecordV3 *desired_record) {
    GimbalNvIo io;
    GimbalNvRecordV3 committed;
    int8_t committed_slot = -1;
    GimbalNvStoreResult result;
    if (storage == NULL || desired_record == NULL) {
        return GIMBAL_NV_STORE_BAD_ARGUMENT;
    }
    io.context = NULL;
    io.read = stm32_read;
    io.erase = stm32_erase;
    io.program = stm32_program;
    result = gimbal_nv_store(
        &io, storage->has_valid_record ? &storage->record : NULL,
        storage->active_slot, desired_record, &committed, &committed_slot);
    if (result == GIMBAL_NV_STORE_OK) {
        storage->record = committed;
        storage->active_slot = committed_slot;
        storage->has_valid_record = true;
        storage->maintenance_required = false;
        storage->slot_status[(unsigned)committed_slot] = GIMBAL_NV_SLOT_VALID;
    }
    return result;
}
