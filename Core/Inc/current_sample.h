#ifndef CURRENT_SAMPLE_H
#define CURRENT_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>

bool current_sample_init(void);
bool current_sample_update(void);
uint16_t current_get_x_raw(void);
uint16_t current_get_y_raw(void);

#endif
