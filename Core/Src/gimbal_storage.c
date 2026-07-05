#include "gimbal_storage.h"

#include "motor_pwm.h"
#include "stm32g4xx_hal.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define GIMBAL_STORAGE_ADDRESS 0x0801F800UL
#define GIMBAL_STORAGE_MAGIC   0x47494D42UL
#define GIMBAL_STORAGE_VERSION 1U

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint16_t x_low;
  uint16_t x_high;
  uint16_t y_low;
  uint16_t y_high;
  uint32_t reserved;
  uint32_t checksum;
} gimbal_storage_record_t;

static bool storage_valid;

static uint32_t record_checksum(const gimbal_storage_record_t *record)
{
  const uint8_t *bytes = (const uint8_t *)record;
  uint32_t hash = 2166136261UL;
  for (size_t i = 0U; i < offsetof(gimbal_storage_record_t, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

static bool record_is_valid(const gimbal_storage_record_t *record)
{
  return record->magic == GIMBAL_STORAGE_MAGIC &&
         record->version == GIMBAL_STORAGE_VERSION &&
         record->size == sizeof(*record) &&
         record->x_low <= 4095U && record->x_high <= 4095U &&
         record->y_low <= 4095U && record->y_high <= 4095U &&
         record->checksum == record_checksum(record);
}

bool gimbal_storage_load(void)
{
  const gimbal_storage_record_t *record =
      (const gimbal_storage_record_t *)GIMBAL_STORAGE_ADDRESS;
  storage_valid = record_is_valid(record);
  if (!storage_valid) return false;

  bool ok = motor_pwm_set_manual_limit_raw(GIMBAL_AXIS_X, true,
                                            record->x_low) &&
            motor_pwm_set_manual_limit_raw(GIMBAL_AXIS_X, false,
                                            record->x_high) &&
            motor_pwm_set_manual_limit_raw(GIMBAL_AXIS_Y, true,
                                            record->y_low) &&
            motor_pwm_set_manual_limit_raw(GIMBAL_AXIS_Y, false,
                                            record->y_high);
  ok = ok && motor_pwm_calibration_is_valid(GIMBAL_AXIS_X) &&
       motor_pwm_calibration_is_valid(GIMBAL_AXIS_Y);
  storage_valid = ok;
  return ok;
}

bool gimbal_storage_save(void)
{
  gimbal_axis_home_t x;
  gimbal_axis_home_t y;
  if (!motor_pwm_get_home_info(GIMBAL_AXIS_X, &x) ||
      !motor_pwm_get_home_info(GIMBAL_AXIS_Y, &y)) {
    return false;
  }

  gimbal_storage_record_t record = {
    .magic = GIMBAL_STORAGE_MAGIC,
    .version = GIMBAL_STORAGE_VERSION,
    .size = sizeof(gimbal_storage_record_t),
    .x_low = x.low_raw,
    .x_high = x.high_raw,
    .y_low = y.low_raw,
    .y_high = y.high_raw,
    .reserved = 0U,
    .checksum = 0U
  };
  record.checksum = record_checksum(&record);

  FLASH_EraseInitTypeDef erase = {
    .TypeErase = FLASH_TYPEERASE_PAGES,
    .Banks = FLASH_BANK_1,
    .Page = 63U,
    .NbPages = 1U
  };
  uint32_t page_error = 0U;
  HAL_FLASH_Unlock();
  bool ok = HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
  for (size_t offset = 0U; ok && offset < sizeof(record);
       offset += sizeof(uint64_t)) {
    uint64_t value;
    memcpy(&value, (const uint8_t *)&record + offset, sizeof(value));
    ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                           GIMBAL_STORAGE_ADDRESS + offset,
                           value) == HAL_OK;
  }
  HAL_FLASH_Lock();

  const gimbal_storage_record_t *stored =
      (const gimbal_storage_record_t *)GIMBAL_STORAGE_ADDRESS;
  storage_valid = ok && memcmp(stored, &record, sizeof(record)) == 0;
  return storage_valid;
}

bool gimbal_storage_clear(void)
{
  FLASH_EraseInitTypeDef erase = {
    .TypeErase = FLASH_TYPEERASE_PAGES,
    .Banks = FLASH_BANK_1,
    .Page = 63U,
    .NbPages = 1U
  };
  uint32_t page_error = 0U;
  HAL_FLASH_Unlock();
  bool ok = HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK;
  HAL_FLASH_Lock();
  if (ok) storage_valid = false;
  return ok;
}

bool gimbal_storage_is_valid(void)
{
  return storage_valid;
}
