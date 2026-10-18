#ifndef CURRENT_SAMPLE_H
#define CURRENT_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint16_t raw[2];
  uint16_t average[2];
  uint16_t peak[2];
  uint32_t window_ms;
  uint32_t samples_since_reset;
  uint32_t sample_age_ms;
  bool valid;
  bool saturated;
} CurrentSampleSnapshot;

bool current_sample_init(void);
bool current_sample_update(void);
void current_sample_reset_statistics(void);
void current_sample_snapshot(CurrentSampleSnapshot *snapshot);
uint16_t current_get_x_raw(void);
uint16_t current_get_y_raw(void);

#endif
