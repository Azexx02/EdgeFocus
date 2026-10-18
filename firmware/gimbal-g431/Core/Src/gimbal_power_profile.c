#include "gimbal_power_profile.h"

#include "gimbal_config.h"

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
  return axis == GIMBAL_POWER_AXIS_P ? GIMBAL_X_HOLD_DUTY_TICKS :
         GIMBAL_Y_HOLD_DUTY_TICKS;
}
