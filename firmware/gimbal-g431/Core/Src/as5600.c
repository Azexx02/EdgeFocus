#include "as5600.h"
#include "i2c.h"
#include "soft_i2c.h"

#define AS5600_ADDRESS 0x36U
#define AS5600_RAW_ANGLE 0x0CU

static uint16_t decode_angle(const uint8_t data[2])
{
  /* AS5600 原始角度寄存器只有低 12 位有效。 */
  return (uint16_t)((((uint16_t)data[0] << 8U) | data[1]) & 0x0FFFU);
}

uint16_t as5600_x_read_raw(void)
{
  /* 逻辑 X 轴编码器：硬件 I2C1。 */
  uint8_t data[2];
  if (HAL_I2C_Mem_Read(&hi2c1, AS5600_ADDRESS << 1U, AS5600_RAW_ANGLE,
                       I2C_MEMADD_SIZE_8BIT, data, sizeof(data), 2U) != HAL_OK) {
    return AS5600_READ_ERROR;
  }
  return decode_angle(data);
}

uint16_t as5600_y_read_raw(void)
{
  /* 逻辑 Y 轴编码器：PB10/PB11 软件 I2C。 */
  uint8_t data[2];
  if (!soft_i2c_mem_read(AS5600_ADDRESS, AS5600_RAW_ANGLE, data, sizeof(data))) {
    return AS5600_READ_ERROR;
  }
  return decode_angle(data);
}
