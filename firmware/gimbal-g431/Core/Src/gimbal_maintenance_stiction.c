#include "gimbal_maintenance_stiction.h"

#include <stddef.h>
#include <string.h>

static void clear_live_state(GimbalMaintenanceStictionState *state)
{
  state->qualifying = false;
  state->qualify_started_ms = 0U;
  state->active = false;
  state->assist_started_ms = 0U;
  state->assist_direction = 0;
  state->assist_attempted = false;
  state->assist_exhausted = false;
  state->assist_floor = 0U;
}

static uint16_t elapsed_u16(uint32_t now, uint32_t then, uint32_t limit)
{
  uint32_t elapsed = now - then;
  if (elapsed > limit) elapsed = limit;
  return (uint16_t)elapsed;
}

static bool control_eligible(const GimbalMaintenanceStictionInput *input)
{
  return input->maintenance_yaw && input->target_ramp_complete &&
         !input->settled &&
         input->error_abs > GIMBAL_MAINT_STICTION_MIN_ERROR_RAW &&
         (input->requested_direction == -1 ||
          input->requested_direction == 1) &&
         input->requested_magnitude != 0U &&
         input->maximum_magnitude != 0U;
}

static bool stalled(const GimbalMaintenanceStictionInput *input)
{
  return control_eligible(input) &&
         input->speed_raw >= -GIMBAL_MAINT_STICTION_MAX_SPEED_RAW &&
         input->speed_raw <= GIMBAL_MAINT_STICTION_MAX_SPEED_RAW;
}

void gimbal_maintenance_stiction_init(GimbalMaintenanceStictionState *state)
{
  if (state == NULL) return;
  memset(state, 0, sizeof(*state));
}

void gimbal_maintenance_stiction_reset_control(
    GimbalMaintenanceStictionState *state)
{
  if (state == NULL) return;
  clear_live_state(state);
}

void gimbal_maintenance_stiction_new_target(
    GimbalMaintenanceStictionState *state, uint32_t target_generation)
{
  if (state == NULL) return;
  clear_live_state(state);
  memset(&state->diagnostics, 0, sizeof(state->diagnostics));
  state->diagnostics.target_generation = target_generation;
}

GimbalMaintenanceStictionOutput gimbal_maintenance_stiction_update(
    GimbalMaintenanceStictionState *state,
    const GimbalMaintenanceStictionInput *input)
{
  GimbalMaintenanceStictionOutput output = {0U, 0, false};
  bool input_eligible;
  uint32_t elapsed;
  uint32_t next_floor;
  if (input == NULL) return output;
  output.magnitude = input->requested_magnitude;
  output.direction = input->requested_direction;
  if (state == NULL) return output;

  input_eligible = stalled(input);
  if (state->active) {
    elapsed = input->now_ms - state->assist_started_ms;
    state->diagnostics.active_ms = elapsed_u16(
        input->now_ms, state->assist_started_ms,
        GIMBAL_MAINT_STICTION_MAX_ACTIVE_MS);
    if (!control_eligible(input) ||
        input->requested_direction != state->assist_direction) {
      state->active = false;
      state->assist_floor = 0U;
      state->qualifying = false;
      output.release_immediately = true;
      return output;
    }
    if (elapsed >= GIMBAL_MAINT_STICTION_MAX_ACTIVE_MS) {
      state->active = false;
      state->assist_floor = 0U;
      state->assist_exhausted = true;
      state->diagnostics.exhausted = true;
      output.release_immediately = true;
      return output;
    }
    next_floor = state->assist_floor;
    if (next_floor < input->requested_magnitude) {
      next_floor = input->requested_magnitude;
    }
    next_floor += GIMBAL_MAINT_STICTION_FLOOR_STEP;
    if (next_floor > input->maximum_magnitude) {
      next_floor = input->maximum_magnitude;
    }
    state->assist_floor = (uint16_t)next_floor;
    if (state->assist_floor > state->diagnostics.peak_floor) {
      state->diagnostics.peak_floor = state->assist_floor;
    }
    if (output.magnitude < state->assist_floor) {
      output.magnitude = state->assist_floor;
    }
    return output;
  }

  state->assist_floor = 0U;
  if (!input_eligible || state->assist_attempted) {
    state->qualifying = false;
    return output;
  }
  if (!state->qualifying) {
    state->qualifying = true;
    state->qualify_started_ms = input->now_ms;
    return output;
  }
  if ((uint32_t)(input->now_ms - state->qualify_started_ms) <
      GIMBAL_MAINT_STICTION_QUALIFY_MS) {
    return output;
  }

  state->qualifying = false;
  state->active = true;
  state->assist_started_ms = input->now_ms;
  state->assist_attempted = true;
  state->assist_direction = input->requested_direction;
  state->diagnostics.attempted = true;
  next_floor = (uint32_t)input->requested_magnitude +
               GIMBAL_MAINT_STICTION_FLOOR_STEP;
  if (next_floor > input->maximum_magnitude) {
    next_floor = input->maximum_magnitude;
  }
  state->assist_floor = (uint16_t)next_floor;
  state->diagnostics.peak_floor = state->assist_floor;
  if (output.magnitude < state->assist_floor) {
    output.magnitude = state->assist_floor;
  }
  return output;
}

void gimbal_maintenance_stiction_get_diagnostics(
    const GimbalMaintenanceStictionState *state,
    GimbalMaintenanceStictionDiagnostics *diagnostics)
{
  if (state == NULL || diagnostics == NULL) return;
  *diagnostics = state->diagnostics;
}
