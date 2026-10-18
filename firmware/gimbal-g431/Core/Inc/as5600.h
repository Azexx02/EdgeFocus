#ifndef AS5600_H
#define AS5600_H

#include <stdint.h>

#define AS5600_READ_ERROR UINT16_MAX
/* X 轴使用硬件 I2C1，Y 轴使用 PB10/PB11 软件 I2C。 */
uint16_t as5600_x_read_raw(void);
uint16_t as5600_y_read_raw(void);

#endif
