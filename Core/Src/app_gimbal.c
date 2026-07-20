#include "app_gimbal.h"

#include "as5600.h"
#include "board_config.h"
#include "current_sample.h"
#include "gimbal_angle.h"
#include "gimbal_config.h"
#include "main.h"
#include "motor_pwm.h"
#include "soft_i2c.h"
#include "uart_protocol.h"

#include <stdio.h>
#include <string.h>

#define POSE_SAMPLE_PERIOD_MS 20U
#define ORIGIN_STABLE_SAMPLES 10U
#define ORIGIN_STABLE_RAW_DELTA 4
#define MAINTENANCE_LIMIT_TENTHS 100

static bool enable_input_was_high;
static GimbalProtocolState runtime_state;
static AppGimbalArmProfile runtime_profile;
static uint32_t motion_generation;
static const char *runtime_fault = "NONE";
static uint16_t sampled_raw[2];
static uint16_t previous_raw[2];
static bool sampled_valid[2];
static uint8_t stable_sample_count;
static uint32_t sample_tick;
static uint32_t next_pose_sample;
static uint32_t operation_generation;
static uint8_t calibration_axis_mask;
static const char *runtime_operation = "NONE";
static const char *last_operation_status = "OK";
static uint8_t peak_violation_count[2];
static uint16_t stall_reference_raw[2];
static uint32_t stall_start_tick[2];

GimbalNvStorage app_gimbal_nv_storage;

static int16_t raw_shortest_delta(uint16_t from, uint16_t to)
{
  int16_t delta = (int16_t)(((uint32_t)to + 4096U - from) & 0x0FFFU);
  if (delta >= 2048) delta = (int16_t)(delta - 4096);
  return delta;
}

static void outputs_off(void)
{
  motor_pwm_hold_enable(false);
  (void)motor_pwm_set_enabled(false);
  board_set_boost(false);
  board_set_dac_mv(GIMBAL_IDLE_DAC_MV, GIMBAL_IDLE_DAC_MV);
  motor_pwm_set_maintenance_mode(false);
}

static void latch_fault(const char *code)
{
  outputs_off();
  runtime_profile = APP_GIMBAL_PROFILE_NONE;
  runtime_fault = code;
  runtime_state = GIMBAL_STATE_FAULT;
  board_set_fault(true);
}

static void reset_current_protection_runtime(void)
{
  memset(peak_violation_count, 0, sizeof(peak_violation_count));
  stall_reference_raw[0] = sampled_raw[0];
  stall_reference_raw[1] = sampled_raw[1];
  stall_start_tick[0] = HAL_GetTick();
  stall_start_tick[1] = stall_start_tick[0];
}

static void check_current_protection(uint32_t now)
{
  CurrentSampleSnapshot current;
  const GimbalCurrentProtectionConfig *config;
  unsigned axis;
  if (runtime_profile != APP_GIMBAL_PROFILE_PRODUCTION ||
      (app_gimbal_nv_storage.record.valid_flags &
       GIMBAL_NV_VALID_CURRENT_PROTECTION) == 0U ||
      (!motor_pwm_is_enabled() && !board_get_boost())) {
    reset_current_protection_runtime();
    return;
  }
  current_sample_snapshot(&current);
  config = &app_gimbal_nv_storage.record.current_protection;
  if (current.saturated) {
    latch_fault("OVERCURRENT");
    return;
  }
  for (axis = 0U; axis < 2U; ++axis) {
    if (current.raw[axis] > config->peak_limit_raw[axis]) {
      if (peak_violation_count[axis] < UINT8_MAX) {
        ++peak_violation_count[axis];
      }
    } else {
      peak_violation_count[axis] = 0U;
    }
    if (peak_violation_count[axis] >= 2U ||
        (current.valid &&
         current.average[axis] > config->average_limit_raw[axis])) {
      latch_fault("OVERCURRENT");
      return;
    }
    if (runtime_state == GIMBAL_STATE_MOVING && sampled_valid[axis] &&
        current.raw[axis] > config->average_limit_raw[axis]) {
      int16_t moved = raw_shortest_delta(stall_reference_raw[axis],
                                         sampled_raw[axis]);
      if (moved > 2 || moved < -2) {
        stall_reference_raw[axis] = sampled_raw[axis];
        stall_start_tick[axis] = now;
      } else if ((uint32_t)(now - stall_start_tick[axis]) >=
                 config->stall_ms) {
        latch_fault("STALL");
        return;
      }
    } else {
      stall_reference_raw[axis] = sampled_raw[axis];
      stall_start_tick[axis] = now;
    }
  }
}

static void apply_persistent_record(void)
{
  const GimbalNvRecordV3 *record = &app_gimbal_nv_storage.record;
  if ((record->valid_flags & GIMBAL_NV_VALID_ELECTRICAL) != 0U) {
    (void)motor_pwm_apply_electrical_calibration(
        GIMBAL_AXIS_P, &record->axis[GIMBAL_AXIS_P]);
    (void)motor_pwm_apply_electrical_calibration(
        GIMBAL_AXIS_Y, &record->axis[GIMBAL_AXIS_Y]);
  }
  if ((record->valid_flags & GIMBAL_NV_VALID_ORIGIN) != 0U) {
    motor_pwm_set_home(GIMBAL_AXIS_P, record->origin_raw[GIMBAL_AXIS_P]);
    motor_pwm_set_home(GIMBAL_AXIS_Y, record->origin_raw[GIMBAL_AXIS_Y]);
  }
}

static bool target_allowed(uint16_t yaw10, uint16_t pitch10)
{
  int16_t p_min = app_gimbal_nv_storage.record.safe_min_tenths[GIMBAL_AXIS_P];
  int16_t p_max = app_gimbal_nv_storage.record.safe_max_tenths[GIMBAL_AXIS_P];
  int16_t y_min = app_gimbal_nv_storage.record.safe_min_tenths[GIMBAL_AXIS_Y];
  int16_t y_max = app_gimbal_nv_storage.record.safe_max_tenths[GIMBAL_AXIS_Y];
  if (runtime_profile == APP_GIMBAL_PROFILE_MAINTENANCE) {
    p_min = y_min = -MAINTENANCE_LIMIT_TENTHS;
    p_max = y_max = MAINTENANCE_LIMIT_TENTHS;
  }
  return yaw10 < GIMBAL_ANGLE10_FULL_TURN &&
         pitch10 < GIMBAL_ANGLE10_FULL_TURN &&
         gimbal_angle10_target_in_window(yaw10, y_min, y_max) &&
         gimbal_angle10_target_in_window(pitch10, p_min, p_max);
}

GimbalNvStoreResult app_gimbal_nv_commit(const GimbalNvRecordV3 *record)
{
  return gimbal_nv_storage_commit(&app_gimbal_nv_storage, record);
}

static void update_pose_sample(uint32_t now)
{
  uint16_t raw[2];
  bool valid[2];
  unsigned axis;
  if ((int32_t)(now - next_pose_sample) < 0) return;
  next_pose_sample = now + POSE_SAMPLE_PERIOD_MS;
  raw[GIMBAL_AXIS_P] = motor_pwm_read_raw(GIMBAL_AXIS_P);
  raw[GIMBAL_AXIS_Y] = motor_pwm_read_raw(GIMBAL_AXIS_Y);
  valid[GIMBAL_AXIS_P] = raw[GIMBAL_AXIS_P] != AS5600_READ_ERROR;
  valid[GIMBAL_AXIS_Y] = raw[GIMBAL_AXIS_Y] != AS5600_READ_ERROR;
  if (valid[0] && valid[1] && sampled_valid[0] && sampled_valid[1] &&
      raw_shortest_delta(previous_raw[0], raw[0]) <= ORIGIN_STABLE_RAW_DELTA &&
      raw_shortest_delta(previous_raw[0], raw[0]) >= -ORIGIN_STABLE_RAW_DELTA &&
      raw_shortest_delta(previous_raw[1], raw[1]) <= ORIGIN_STABLE_RAW_DELTA &&
      raw_shortest_delta(previous_raw[1], raw[1]) >= -ORIGIN_STABLE_RAW_DELTA) {
    if (stable_sample_count < UINT8_MAX) ++stable_sample_count;
  } else {
    stable_sample_count = 0U;
  }
  for (axis = 0U; axis < 2U; ++axis) {
    sampled_raw[axis] = raw[axis];
    sampled_valid[axis] = valid[axis];
    if (valid[axis]) previous_raw[axis] = raw[axis];
  }
  if (valid[0] && valid[1]) sample_tick = now;
}

static void send_reset_cause(void)
{
  char response[80];
  snprintf(response, sizeof(response),
           "BOOT,RESET=BOR:%u,PIN:%u,SOFT:%u,IWDG:%u,WWDG:%u\r\n",
           __HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) ? 1U : 0U,
           __HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) ? 1U : 0U);
  uart_protocol_send(response);
  __HAL_RCC_CLEAR_RESET_FLAGS();
}

AppGimbalResult app_gimbal_arm_profile(AppGimbalArmProfile profile)
{
  uint32_t flags;
  if (runtime_state != GIMBAL_STATE_DISABLED) return APP_GIMBAL_INVALID_STATE;
  if (profile != APP_GIMBAL_PROFILE_PRODUCTION &&
      profile != APP_GIMBAL_PROFILE_MAINTENANCE) return APP_GIMBAL_BAD_RANGE;
  flags = app_gimbal_nv_storage.record.valid_flags;
  if ((flags & GIMBAL_NV_VALID_ELECTRICAL) == 0U) return APP_GIMBAL_CAL_REQUIRED;
  if ((flags & GIMBAL_NV_VALID_ORIGIN) == 0U) return APP_GIMBAL_ORIGIN_REQUIRED;
  if (profile == APP_GIMBAL_PROFILE_PRODUCTION &&
      (flags & GIMBAL_NV_VALID_CURRENT_PROTECTION) == 0U) {
    return APP_GIMBAL_CURRENT_CONFIG_REQUIRED;
  }
  runtime_state = GIMBAL_STATE_ARMING;
  apply_persistent_record();
  outputs_off();
  motor_pwm_set_maintenance_mode(profile == APP_GIMBAL_PROFILE_MAINTENANCE);
  if (!motor_pwm_set_enabled(true)) {
    latch_fault("DRIVER_FAULT");
    return APP_GIMBAL_DRIVER_FAULT;
  }
  board_set_fault(false);
  runtime_fault = "NONE";
  runtime_profile = profile;
  runtime_state = GIMBAL_STATE_READY;
  return APP_GIMBAL_OK;
}

bool app_gimbal_arm(void)
{
  return app_gimbal_arm_profile(APP_GIMBAL_PROFILE_PRODUCTION) == APP_GIMBAL_OK;
}

AppGimbalResult app_gimbal_stop(void)
{
  const bool faulted = runtime_state == GIMBAL_STATE_FAULT;
  if (runtime_state == GIMBAL_STATE_CALIBRATING) {
    motor_pwm_calibration_abort();
    runtime_operation = "NONE";
    last_operation_status = "ABORTED";
  }
  outputs_off();
  runtime_profile = APP_GIMBAL_PROFILE_NONE;
  if (!faulted) {
    runtime_state = GIMBAL_STATE_DISABLED;
    runtime_fault = "NONE";
    board_set_fault(false);
  }
  return APP_GIMBAL_OK;
}

void app_gimbal_emergency_stop(void)
{
  (void)app_gimbal_stop();
}

void app_gimbal_disarm(void)
{
  (void)app_gimbal_stop();
}

bool app_gimbal_is_armed(void)
{
  return runtime_state == GIMBAL_STATE_READY ||
         runtime_state == GIMBAL_STATE_HOLDING ||
         runtime_state == GIMBAL_STATE_MOVING;
}

GimbalProtocolState app_gimbal_state(void)
{
  return runtime_state;
}

AppGimbalResult app_gimbal_hold_now(uint32_t *generation)
{
  uint16_t yaw10;
  uint16_t pitch10;
  if (!gimbal_protocol_command_allowed(runtime_state, GIMBAL_CMD_HOLD_NOW)) {
    return APP_GIMBAL_INVALID_STATE;
  }
  if (!motor_pwm_get_angle_tenths(GIMBAL_AXIS_Y, &yaw10) ||
      !motor_pwm_get_angle_tenths(GIMBAL_AXIS_P, &pitch10)) {
    return APP_GIMBAL_SAMPLE_STALE;
  }
  if (!target_allowed(yaw10, pitch10)) return APP_GIMBAL_BAD_RANGE;
  motor_pwm_hold_enable(true);
  if (!motor_pwm_hold_is_enabled()) {
    latch_fault("DRIVER_FAULT");
    return APP_GIMBAL_DRIVER_FAULT;
  }
  ++motion_generation;
  runtime_state = GIMBAL_STATE_HOLDING;
  if (generation != NULL) *generation = motion_generation;
  return APP_GIMBAL_OK;
}

AppGimbalResult app_gimbal_track(uint16_t yaw10, uint16_t pitch10,
                                 uint32_t *generation)
{
  if (!gimbal_protocol_command_allowed(runtime_state, GIMBAL_CMD_TRACK_YP)) {
    return APP_GIMBAL_INVALID_STATE;
  }
  if (!target_allowed(yaw10, pitch10)) return APP_GIMBAL_BAD_RANGE;
  if (!motor_pwm_hold_is_enabled() ||
      !motor_pwm_hold_set_target_tenths(GIMBAL_AXIS_Y, yaw10) ||
      !motor_pwm_hold_set_target_tenths(GIMBAL_AXIS_P, pitch10)) {
    latch_fault("CONTROL_LOST");
    return APP_GIMBAL_DRIVER_FAULT;
  }
  ++motion_generation;
  runtime_state = GIMBAL_STATE_MOVING;
  if (generation != NULL) *generation = motion_generation;
  return APP_GIMBAL_OK;
}

AppGimbalResult app_gimbal_origin_capture(void)
{
  GimbalNvRecordV3 record;
  const bool resume_hold = runtime_state == GIMBAL_STATE_HOLDING;
  if (!gimbal_protocol_command_allowed(runtime_state,
                                       GIMBAL_CMD_ORIGIN_CAPTURE)) {
    return APP_GIMBAL_INVALID_STATE;
  }
  if ((app_gimbal_nv_storage.record.valid_flags &
       GIMBAL_NV_VALID_ELECTRICAL) == 0U) return APP_GIMBAL_CAL_REQUIRED;
  if (!sampled_valid[0] || !sampled_valid[1] ||
      stable_sample_count < ORIGIN_STABLE_SAMPLES) return APP_GIMBAL_SAMPLE_STALE;
  if (resume_hold) motor_pwm_hold_enable(false);
  record = app_gimbal_nv_storage.record;
  record.origin_raw[GIMBAL_AXIS_P] = sampled_raw[GIMBAL_AXIS_P];
  record.origin_raw[GIMBAL_AXIS_Y] = sampled_raw[GIMBAL_AXIS_Y];
  record.valid_flags |= GIMBAL_NV_VALID_ORIGIN;
  ++record.origin_epoch;
  if (app_gimbal_nv_commit(&record) != GIMBAL_NV_STORE_OK) {
    if (resume_hold) motor_pwm_hold_enable(true);
    return APP_GIMBAL_NVM_FAILED;
  }
  motor_pwm_set_home(GIMBAL_AXIS_P, sampled_raw[GIMBAL_AXIS_P]);
  motor_pwm_set_home(GIMBAL_AXIS_Y, sampled_raw[GIMBAL_AXIS_Y]);
  if (resume_hold) {
    motor_pwm_hold_enable(true);
    if (!motor_pwm_hold_is_enabled()) {
      latch_fault("DRIVER_FAULT");
      return APP_GIMBAL_DRIVER_FAULT;
    }
  }
  return APP_GIMBAL_OK;
}

AppGimbalResult app_gimbal_nvm_clear_origin(void)
{
  GimbalNvRecordV3 record;
  if (runtime_state != GIMBAL_STATE_DISABLED) return APP_GIMBAL_INVALID_STATE;
  record = app_gimbal_nv_storage.record;
  gimbal_nv_clear_origin(&record);
  return app_gimbal_nv_commit(&record) == GIMBAL_NV_STORE_OK ?
         APP_GIMBAL_OK : APP_GIMBAL_NVM_FAILED;
}

AppGimbalResult app_gimbal_nvm_clear_all(void)
{
  GimbalNvRecordV3 record;
  if (runtime_state != GIMBAL_STATE_DISABLED) return APP_GIMBAL_INVALID_STATE;
  record = app_gimbal_nv_storage.record;
  gimbal_nv_clear_all(&record);
  if (app_gimbal_nv_commit(&record) != GIMBAL_NV_STORE_OK) {
    return APP_GIMBAL_NVM_FAILED;
  }
  motor_pwm_clear_calibration();
  return APP_GIMBAL_OK;
}

AppGimbalResult app_gimbal_set_current_protection(
    const GimbalCurrentProtectionConfig *config)
{
  GimbalNvRecordV3 record;
  if (runtime_state != GIMBAL_STATE_DISABLED) return APP_GIMBAL_INVALID_STATE;
  if (config == NULL) return APP_GIMBAL_BAD_RANGE;
  record = app_gimbal_nv_storage.record;
  record.current_protection = *config;
  record.valid_flags |= GIMBAL_NV_VALID_CURRENT_PROTECTION;
  if (!gimbal_nv_record_valid(&record)) return APP_GIMBAL_BAD_RANGE;
  return app_gimbal_nv_commit(&record) == GIMBAL_NV_STORE_OK ?
         APP_GIMBAL_OK : APP_GIMBAL_NVM_FAILED;
}

AppGimbalResult app_gimbal_clear_current_protection(void)
{
  GimbalNvRecordV3 record;
  if (runtime_state != GIMBAL_STATE_DISABLED) return APP_GIMBAL_INVALID_STATE;
  record = app_gimbal_nv_storage.record;
  memset(&record.current_protection, 0, sizeof(record.current_protection));
  record.valid_flags &= ~GIMBAL_NV_VALID_CURRENT_PROTECTION;
  return app_gimbal_nv_commit(&record) == GIMBAL_NV_STORE_OK ?
         APP_GIMBAL_OK : APP_GIMBAL_NVM_FAILED;
}

AppGimbalResult app_gimbal_fault_clear(void)
{
  CurrentSampleSnapshot current;
  unsigned axis;
  if (runtime_state != GIMBAL_STATE_FAULT) return APP_GIMBAL_INVALID_STATE;
  if (motor_pwm_is_enabled() || motor_pwm_hold_is_enabled() ||
      board_get_boost()) return APP_GIMBAL_BUSY;
  if (!sampled_valid[0] || !sampled_valid[1]) return APP_GIMBAL_SAMPLE_STALE;
  current_sample_snapshot(&current);
  if (current.saturated) return APP_GIMBAL_DRIVER_FAULT;
  if ((app_gimbal_nv_storage.record.valid_flags &
       GIMBAL_NV_VALID_CURRENT_PROTECTION) != 0U) {
    for (axis = 0U; axis < 2U; ++axis) {
      if (current.raw[axis] >= app_gimbal_nv_storage.record.
              current_protection.peak_limit_raw[axis]) {
        return APP_GIMBAL_DRIVER_FAULT;
      }
    }
  }
  board_set_fault(false);
  runtime_fault = "NONE";
  runtime_state = GIMBAL_STATE_DISABLED;
  return APP_GIMBAL_OK;
}

AppGimbalResult app_gimbal_calibration_start(GimbalProtocolAxis axis,
                                             uint32_t *op_generation)
{
  uint8_t mask;
  if (runtime_state != GIMBAL_STATE_DISABLED) return APP_GIMBAL_INVALID_STATE;
  if (axis == GIMBAL_PROTOCOL_AXIS_P) mask = MOTOR_PWM_CAL_AXIS_P;
  else if (axis == GIMBAL_PROTOCOL_AXIS_Y) mask = MOTOR_PWM_CAL_AXIS_Y;
  else if (axis == GIMBAL_PROTOCOL_AXIS_ALL) mask = MOTOR_PWM_CAL_AXIS_ALL;
  else return APP_GIMBAL_BAD_RANGE;
  if (mask != MOTOR_PWM_CAL_AXIS_ALL &&
      (app_gimbal_nv_storage.record.valid_flags &
       GIMBAL_NV_VALID_ELECTRICAL) == 0U) return APP_GIMBAL_CAL_REQUIRED;
  if (!motor_pwm_calibration_begin(mask)) return APP_GIMBAL_BUSY;
  ++operation_generation;
  calibration_axis_mask = mask;
  runtime_operation = "CAL_ELECTRICAL";
  last_operation_status = "RUNNING";
  runtime_state = GIMBAL_STATE_CALIBRATING;
  if (op_generation != NULL) *op_generation = operation_generation;
  return APP_GIMBAL_OK;
}

void app_gimbal_snapshot(AppGimbalSnapshot *snapshot)
{
  uint16_t yaw10;
  uint16_t pitch10;
  uint16_t target_yaw10;
  uint16_t target_pitch10;
  uint32_t primask;
  if (snapshot == NULL) return;
  primask = __get_PRIMASK();
  __disable_irq();
  memset(snapshot, 0, sizeof(*snapshot));
  snapshot->state = runtime_state;
  snapshot->arm_profile = runtime_profile;
  snapshot->p_raw = sampled_raw[GIMBAL_AXIS_P];
  snapshot->y_raw = sampled_raw[GIMBAL_AXIS_Y];
  snapshot->p_raw_valid = sampled_valid[GIMBAL_AXIS_P];
  snapshot->y_raw_valid = sampled_valid[GIMBAL_AXIS_Y];
  snapshot->motion_generation = motion_generation;
  snapshot->calibration_epoch = app_gimbal_nv_storage.record.calibration_epoch;
  snapshot->origin_epoch = app_gimbal_nv_storage.record.origin_epoch;
  snapshot->valid_flags = app_gimbal_nv_storage.record.valid_flags;
  snapshot->fault_code = runtime_fault;
  snapshot->operation = runtime_operation;
  snapshot->operation_generation = operation_generation;
  snapshot->operation_progress = strcmp(runtime_operation, "NONE") == 0 ?
                                 0U : motor_pwm_calibration_progress();
  snapshot->last_operation_status = last_operation_status;
  snapshot->sample_age_ms = HAL_GetTick() - sample_tick;
  __set_PRIMASK(primask);
  snapshot->logical_angles_valid =
      motor_pwm_get_angle_tenths(GIMBAL_AXIS_Y, &yaw10) &&
      motor_pwm_get_angle_tenths(GIMBAL_AXIS_P, &pitch10);
  if (snapshot->logical_angles_valid) {
    snapshot->yaw10 = yaw10;
    snapshot->pitch10 = pitch10;
  }
  snapshot->target_valid = motor_pwm_hold_is_enabled() &&
      motor_pwm_get_target_tenths(GIMBAL_AXIS_Y, &target_yaw10) &&
      motor_pwm_get_target_tenths(GIMBAL_AXIS_P, &target_pitch10);
  if (snapshot->target_valid) {
    snapshot->target_yaw10 = target_yaw10;
    snapshot->target_pitch10 = target_pitch10;
  }
  snapshot->settled = snapshot->target_valid && motor_pwm_all_settled();
}

const char *app_gimbal_result_code(AppGimbalResult result)
{
  static const char *const codes[] = {
    "OK", "INVALID_STATE", "CAL_REQUIRED", "ORIGIN_REQUIRED",
    "CURRENT_CONFIG_REQUIRED", "SAMPLE_STALE", "BAD_RANGE", "BUSY",
    "DRIVER_FAULT", "NVM_FAILED"
  };
  return result <= APP_GIMBAL_NVM_FAILED ? codes[result] : "UNKNOWN";
}

const char *app_gimbal_profile_name(AppGimbalArmProfile profile)
{
  if (profile == APP_GIMBAL_PROFILE_PRODUCTION) return "PRODUCTION";
  if (profile == APP_GIMBAL_PROFILE_MAINTENANCE) return "MAINTENANCE";
  return "NONE";
}

void app_gimbal_init(void)
{
  char response[120];
  board_config_init();
  soft_i2c_init();
  motor_pwm_init();
  outputs_off();
  gimbal_nv_storage_init(&app_gimbal_nv_storage);
  apply_persistent_record();
  (void)current_sample_init();
  uart_protocol_init();
  runtime_state = GIMBAL_STATE_DISABLED;
  runtime_profile = APP_GIMBAL_PROFILE_NONE;
  runtime_fault = "NONE";
  motion_generation = 0U;
  operation_generation = 0U;
  calibration_axis_mask = 0U;
  runtime_operation = "NONE";
  last_operation_status = "OK";
  stable_sample_count = 0U;
  sample_tick = HAL_GetTick();
  next_pose_sample = sample_tick;
  memset(sampled_valid, 0, sizeof(sampled_valid));
  reset_current_protection_runtime();
  enable_input_was_high = board_get_enable_input();
  board_set_fault(false);
  send_reset_cause();
  snprintf(response, sizeof(response),
           "BOOT,NVM=V3,VALID=%u,SLOT=%d,GEN=%lu,FLAGS=%lu,MAINT=%u\r\n",
           app_gimbal_nv_storage.has_valid_record ? 1U : 0U,
           (int)app_gimbal_nv_storage.active_slot,
           (unsigned long)app_gimbal_nv_storage.record.generation,
           (unsigned long)app_gimbal_nv_storage.record.valid_flags,
           app_gimbal_nv_storage.maintenance_required ? 1U : 0U);
  uart_protocol_send(response);
  uart_protocol_send("BOOT,MODE=UART_ONLY,STATE=DISABLED\r\n");
}

void app_gimbal_process(void)
{
  static uint32_t next_current_sample;
  uint32_t now = HAL_GetTick();
#if GIMBAL_REQUIRE_ENABLE_INPUT
  const bool enable_input_high = board_get_enable_input();
  if (enable_input_was_high && !enable_input_high &&
      runtime_state != GIMBAL_STATE_DISABLED) {
    latch_fault("DRIVER_FAULT");
  }
  enable_input_was_high = enable_input_high;
#endif
  uart_protocol_process();
  if (runtime_state == GIMBAL_STATE_CALIBRATING) {
    motor_pwm_calibration_status_t calibration_status;
    motor_pwm_calibration_process();
    calibration_status = motor_pwm_calibration_status();
    if (calibration_status == MOTOR_PWM_CAL_SUCCEEDED) {
      GimbalNvRecordV3 record = app_gimbal_nv_storage.record;
      bool exported = true;
      if ((calibration_axis_mask & MOTOR_PWM_CAL_AXIS_P) != 0U) {
        exported = motor_pwm_get_electrical_calibration(
            GIMBAL_AXIS_P, &record.axis[GIMBAL_AXIS_P]) && exported;
      }
      if ((calibration_axis_mask & MOTOR_PWM_CAL_AXIS_Y) != 0U) {
        exported = motor_pwm_get_electrical_calibration(
            GIMBAL_AXIS_Y, &record.axis[GIMBAL_AXIS_Y]) && exported;
      }
      if (calibration_axis_mask == MOTOR_PWM_CAL_AXIS_ALL) {
        record.valid_flags |= GIMBAL_NV_VALID_ELECTRICAL;
      }
      ++record.calibration_epoch;
      if (!exported || app_gimbal_nv_commit(&record) != GIMBAL_NV_STORE_OK) {
        apply_persistent_record();
        runtime_operation = "NONE";
        last_operation_status = "FAILED";
        latch_fault("NVM_FAILED");
      } else {
        runtime_operation = "NONE";
        last_operation_status = "OK";
        runtime_state = GIMBAL_STATE_DISABLED;
      }
    } else if (calibration_status == MOTOR_PWM_CAL_FAILED) {
      runtime_operation = "NONE";
      last_operation_status = "FAILED";
      latch_fault("ENCODER_FAULT");
    }
  }
  motor_pwm_hold_process();
  if (board_get_fault() && runtime_state != GIMBAL_STATE_FAULT) {
    latch_fault("CONTROL_LOST");
  }
  if (runtime_state == GIMBAL_STATE_MOVING && motor_pwm_all_settled()) {
    runtime_state = GIMBAL_STATE_HOLDING;
  }
  update_pose_sample(now);
  if ((int32_t)(now - next_current_sample) >= 0) {
    (void)current_sample_update();
    next_current_sample = now + 10U;
    check_current_protection(now);
  }
}
