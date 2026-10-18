#ifndef APP_GIMBAL_H
#define APP_GIMBAL_H

#include <stdbool.h>
#include <stdint.h>

#include "gimbal_nv_stm32.h"
#include "gimbal_protocol.h"

typedef enum {
  APP_GIMBAL_PROFILE_NONE = 0,
  APP_GIMBAL_PROFILE_PRODUCTION,
  APP_GIMBAL_PROFILE_MAINTENANCE
} AppGimbalArmProfile;

typedef enum {
  APP_GIMBAL_OK = 0,
  APP_GIMBAL_INVALID_STATE,
  APP_GIMBAL_CAL_REQUIRED,
  APP_GIMBAL_ORIGIN_REQUIRED,
  APP_GIMBAL_CURRENT_CONFIG_REQUIRED,
  APP_GIMBAL_SAMPLE_STALE,
  APP_GIMBAL_BAD_RANGE,
  APP_GIMBAL_BUSY,
  APP_GIMBAL_DRIVER_FAULT,
  APP_GIMBAL_NVM_FAILED
} AppGimbalResult;

typedef struct {
  GimbalProtocolState state;
  AppGimbalArmProfile arm_profile;
  uint16_t p_raw;
  uint16_t y_raw;
  bool p_raw_valid;
  bool y_raw_valid;
  uint16_t yaw10;
  uint16_t pitch10;
  bool logical_angles_valid;
  uint16_t target_yaw10;
  uint16_t target_pitch10;
  bool target_valid;
  uint32_t motion_generation;
  bool settled;
  uint32_t calibration_epoch;
  uint32_t origin_epoch;
  uint32_t valid_flags;
  uint32_t sample_age_ms;
  const char *fault_code;
  const char *operation;
  uint32_t operation_generation;
  uint8_t operation_progress;
  const char *last_operation_status;
} AppGimbalSnapshot;

extern GimbalNvStorage app_gimbal_nv_storage;

void app_gimbal_init(void);
void app_gimbal_process(void);
bool app_gimbal_arm(void);
AppGimbalResult app_gimbal_arm_profile(AppGimbalArmProfile profile);
void app_gimbal_disarm(void);
AppGimbalResult app_gimbal_stop(void);
void app_gimbal_emergency_stop(void);
bool app_gimbal_is_armed(void);
GimbalProtocolState app_gimbal_state(void);
AppGimbalResult app_gimbal_hold_now(uint32_t *generation);
AppGimbalResult app_gimbal_track(uint16_t yaw10, uint16_t pitch10,
                                 uint32_t *generation);
AppGimbalResult app_gimbal_origin_capture(void);
AppGimbalResult app_gimbal_nvm_clear_origin(void);
AppGimbalResult app_gimbal_nvm_clear_all(void);
AppGimbalResult app_gimbal_set_current_protection(
    const GimbalCurrentProtectionConfig *config);
AppGimbalResult app_gimbal_clear_current_protection(void);
AppGimbalResult app_gimbal_fault_clear(void);
AppGimbalResult app_gimbal_calibration_start(GimbalProtocolAxis axis,
                                             uint32_t *operation_generation);
void app_gimbal_snapshot(AppGimbalSnapshot *snapshot);
const char *app_gimbal_result_code(AppGimbalResult result);
const char *app_gimbal_profile_name(AppGimbalArmProfile profile);
GimbalNvStoreResult app_gimbal_nv_commit(const GimbalNvRecordV3 *record);

#endif
