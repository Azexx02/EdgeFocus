#ifndef GIMBAL_STORAGE_H
#define GIMBAL_STORAGE_H

#include <stdbool.h>

bool gimbal_storage_load(void);
bool gimbal_storage_save(void);
bool gimbal_storage_clear(void);
bool gimbal_storage_is_valid(void);

#endif
