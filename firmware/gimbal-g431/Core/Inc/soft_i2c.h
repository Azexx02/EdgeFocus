#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdbool.h>
#include <stdint.h>

void soft_i2c_init(void);
/* 读取 7 位地址设备的连续寄存器数据。 */
bool soft_i2c_mem_read(uint8_t address_7bit, uint8_t reg, uint8_t *data, uint16_t length);

#endif
