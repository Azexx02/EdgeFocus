#include "gimbal_power_profile.h"

#include "gimbal_config.h"

#if GIMBAL_X_MAINTENANCE_MAX_DUTY_TICKS < GIMBAL_X_HOLD_MIN_DUTY_TICKS
#error "Pitch maintenance maximum must not be below its minimum"
#endif
#if GIMBAL_Y_MAINTENANCE_MAX_DUTY_TICKS < GIMBAL_Y_HOLD_MIN_DUTY_TICKS
#error "Yaw maintenance maximum must not be below its minimum"
#endif
#if GIMBAL_X_MAINTENANCE_MAX_DUTY_TICKS > GIMBAL_X_HOLD_DUTY_TICKS
#error "Pitch maintenance maximum must not exceed production maximum"
#endif
#if GIMBAL_Y_MAINTENANCE_MAX_DUTY_TICKS > GIMBAL_Y_HOLD_DUTY_TICKS
#error "Yaw maintenance maximum must not exceed production maximum"
#endif

static bool maintenance_mode;

void gimbal_power_profile_reset(void)
{
  maintenance_mode = false;
}

void gimbal_power_profile_set_maintenance(bool enabled)
{
  maintenance_mode = enabled;
}

bool gimbal_power_profile_is_maintenance(void)
{
  return maintenance_mode;
}

uint32_t gimbal_power_profile_hold_min(GimbalPowerAxis axis)
{
  return axis == GIMBAL_POWER_AXIS_P ? GIMBAL_X_HOLD_MIN_DUTY_TICKS :
         GIMBAL_Y_HOLD_MIN_DUTY_TICKS;
}

uint32_t gimbal_power_profile_hold_max(GimbalPowerAxis axis)
{
  if (maintenance_mode) {
    return axis == GIMBAL_POWER_AXIS_P ?
           GIMBAL_X_MAINTENANCE_MAX_DUTY_TICKS :
           GIMBAL_Y_MAINTENANCE_MAX_DUTY_TICKS;
  }
  return axis == GIMBAL_POWER_AXIS_P ? GIMBAL_X_HOLD_DUTY_TICKS :
         GIMBAL_Y_HOLD_DUTY_TICKS;
}
