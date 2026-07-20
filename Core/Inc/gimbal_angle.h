#ifndef GIMBAL_ANGLE_H
#define GIMBAL_ANGLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIMBAL_ANGLE10_FULL_TURN 3600
#define GIMBAL_ENCODER_RAW_FULL_TURN 4096

uint16_t gimbal_angle10_wrap(int32_t angle10);
int16_t gimbal_angle10_shortest_delta(uint16_t from10, uint16_t to10);
bool gimbal_angle10_offset_in_window(int16_t offset10, int16_t minimum10,
                                     int16_t maximum10);
bool gimbal_angle10_target_in_window(uint16_t target10, int16_t minimum10,
                                     int16_t maximum10);
uint16_t gimbal_raw_to_angle10(uint16_t raw, uint16_t origin_raw,
                               int8_t positive_raw_sign);

#ifdef __cplusplus
}
#endif

#endif
