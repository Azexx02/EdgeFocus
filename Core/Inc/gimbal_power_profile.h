#ifndef GIMBAL_POWER_PROFILE_H
#define GIMBAL_POWER_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  GIMBAL_POWER_AXIS_P = 0,
  GIMBAL_POWER_AXIS_Y = 1
} GimbalPowerAxis;

void gimbal_power_profile_reset(void);
void gimbal_power_profile_set_maintenance(bool enabled);
bool gimbal_power_profile_is_maintenance(void);
uint32_t gimbal_power_profile_hold_min(GimbalPowerAxis axis);
uint32_t gimbal_power_profile_hold_max(GimbalPowerAxis axis);

#endif
