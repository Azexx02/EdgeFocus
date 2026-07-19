#ifndef APP_GIMBAL_H
#define APP_GIMBAL_H

#include <stdbool.h>

void app_gimbal_init(void);
void app_gimbal_process(void);
bool app_gimbal_arm(void);
void app_gimbal_disarm(void);
bool app_gimbal_is_armed(void);

#endif
