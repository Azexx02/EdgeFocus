#ifndef APP_GIMBAL_H
#define APP_GIMBAL_H

#include <stdbool.h>
#include "gimbal_nv_stm32.h"

extern GimbalNvStorage app_gimbal_nv_storage;

void app_gimbal_init(void);
void app_gimbal_process(void);
bool app_gimbal_arm(void);
void app_gimbal_disarm(void);
bool app_gimbal_is_armed(void);
GimbalNvStoreResult app_gimbal_nv_commit(const GimbalNvRecordV3 *record);

#endif
