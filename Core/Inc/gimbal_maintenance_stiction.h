#ifndef GIMBAL_MAINTENANCE_STICTION_H
#define GIMBAL_MAINTENANCE_STICTION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIMBAL_MAINT_STICTION_QUALIFY_MS 150U
#define GIMBAL_MAINT_STICTION_MAX_ACTIVE_MS 300U
#define GIMBAL_MAINT_STICTION_RECOVERY_MS 50U
#define GIMBAL_MAINT_STICTION_FLOOR_STEP 2U
#define GIMBAL_MAINT_STICTION_MIN_ERROR_RAW 8U
#define GIMBAL_MAINT_STICTION_MAX_SPEED_RAW 1

typedef struct {
  bool maintenance_yaw;
  bool target_ramp_complete;
  bool settled;
  uint16_t error_abs;
  int32_t speed_raw;
  uint16_t requested_magnitude;
  int8_t requested_direction;
  uint16_t maximum_magnitude;
  uint32_t now_ms;
} GimbalMaintenanceStictionInput;

typedef struct {
  uint16_t magnitude;
  int8_t direction;
  bool release_immediately;
} GimbalMaintenanceStictionOutput;

typedef struct {
  uint32_t target_generation;
  bool attempted;
  bool exhausted;
  uint16_t active_ms;
  uint16_t peak_floor;
} GimbalMaintenanceStictionDiagnostics;

typedef struct {
  bool qualifying;
  uint32_t qualify_started_ms;
  bool active;
  uint32_t assist_started_ms;
  bool recovering;
  uint32_t recovery_started_ms;
  int8_t assist_direction;
  bool assist_attempted;
  bool assist_exhausted;
  uint16_t assist_floor;
  GimbalMaintenanceStictionDiagnostics diagnostics;
} GimbalMaintenanceStictionState;

void gimbal_maintenance_stiction_init(GimbalMaintenanceStictionState *state);
void gimbal_maintenance_stiction_reset_control(
    GimbalMaintenanceStictionState *state);
void gimbal_maintenance_stiction_new_target(
    GimbalMaintenanceStictionState *state, uint32_t target_generation);
GimbalMaintenanceStictionOutput gimbal_maintenance_stiction_update(
    GimbalMaintenanceStictionState *state,
    const GimbalMaintenanceStictionInput *input);
void gimbal_maintenance_stiction_get_diagnostics(
    const GimbalMaintenanceStictionState *state,
    GimbalMaintenanceStictionDiagnostics *diagnostics);

#ifdef __cplusplus
}
#endif

#endif
