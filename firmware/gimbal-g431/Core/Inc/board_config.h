#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

void board_config_init(void);
void board_set_boost(bool enabled);
bool board_get_boost(void);
bool board_get_enable_input(void);
bool board_get_boot0_ctrl(void);
void board_set_fault(bool active);
bool board_get_fault(void);
/* 参数按逻辑云台 X/Y 轴排列，函数内部完成物理 DAC 通道映射。 */
void board_set_dac_mv(uint16_t x_mv, uint16_t y_mv);

#endif
