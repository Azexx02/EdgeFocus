#include "gimbal_angle.h"

uint16_t gimbal_angle10_wrap(int32_t angle10) {
    int32_t wrapped = angle10 % GIMBAL_ANGLE10_FULL_TURN;
    if (wrapped < 0) wrapped += GIMBAL_ANGLE10_FULL_TURN;
    return (uint16_t)wrapped;
}

int16_t gimbal_angle10_shortest_delta(uint16_t from10, uint16_t to10) {
    int32_t delta = (int32_t)gimbal_angle10_wrap(to10) -
                    (int32_t)gimbal_angle10_wrap(from10);
    if (delta >= GIMBAL_ANGLE10_FULL_TURN / 2) {
        delta -= GIMBAL_ANGLE10_FULL_TURN;
    }
    if (delta < -(GIMBAL_ANGLE10_FULL_TURN / 2)) {
        delta += GIMBAL_ANGLE10_FULL_TURN;
    }
    return (int16_t)delta;
}

bool gimbal_angle10_offset_in_window(int16_t offset10, int16_t minimum10,
                                     int16_t maximum10) {
    if (minimum10 > 0 || maximum10 < 0 || minimum10 >= maximum10) return false;
    if (minimum10 < -1800 || maximum10 > 1799) return false;
    return offset10 >= minimum10 && offset10 <= maximum10;
}

bool gimbal_angle10_target_in_window(uint16_t target10, int16_t minimum10,
                                     int16_t maximum10) {
    return gimbal_angle10_offset_in_window(
        gimbal_angle10_shortest_delta(0U, target10), minimum10, maximum10);
}

bool gimbal_angle10_profile_target_allowed(
    uint16_t yaw10, uint16_t pitch10,
    int16_t yaw_min10, int16_t yaw_max10,
    int16_t pitch_min10, int16_t pitch_max10,
    bool maintenance_profile, bool hold_operation,
    int16_t maintenance_limit10) {
    if (maintenance_profile && !hold_operation) {
        if (maintenance_limit10 <= 0 || maintenance_limit10 > 1799) {
            return false;
        }
        yaw_min10 = pitch_min10 = (int16_t)-maintenance_limit10;
        yaw_max10 = pitch_max10 = maintenance_limit10;
    }
    return yaw10 < GIMBAL_ANGLE10_FULL_TURN &&
           pitch10 < GIMBAL_ANGLE10_FULL_TURN &&
           gimbal_angle10_target_in_window(yaw10, yaw_min10, yaw_max10) &&
           gimbal_angle10_target_in_window(pitch10, pitch_min10,
                                           pitch_max10);
}

uint16_t gimbal_raw_to_angle10(uint16_t raw, uint16_t origin_raw,
                               int8_t positive_raw_sign) {
    int32_t delta = ((int32_t)(raw & 0x0FFFU) -
                     (int32_t)(origin_raw & 0x0FFFU) + 2048) & 0x0FFF;
    delta -= 2048;
    if (positive_raw_sign < 0) delta = -delta;
    int32_t scaled = delta * GIMBAL_ANGLE10_FULL_TURN;
    scaled = scaled >= 0 ? (scaled + 2048) / GIMBAL_ENCODER_RAW_FULL_TURN
                         : (scaled - 2048) / GIMBAL_ENCODER_RAW_FULL_TURN;
    return gimbal_angle10_wrap(scaled);
}
