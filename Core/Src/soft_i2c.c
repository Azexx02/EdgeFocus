#include "soft_i2c.h"
#include "main.h"

#define I2C_HALF_PERIOD_US 10U /* 联调阶段使用保守的 50 kHz。 */
#define I2C_CLOCK_TIMEOUT_US 200U

static void delay_us(uint32_t us)
{
  const uint32_t start = DWT->CYCCNT;
  const uint32_t ticks = (SystemCoreClock / 1000000U) * us;
  while ((uint32_t)(DWT->CYCCNT - start) < ticks) { }
}

static void scl_release(void)
{
  SOFT_I2C_SCL_GPIO_Port->BSRR = SOFT_I2C_SCL_Pin;
}

static void scl_low(void)
{
  SOFT_I2C_SCL_GPIO_Port->BSRR = (uint32_t)SOFT_I2C_SCL_Pin << 16U;
}

static void sda_release(void)
{
  SOFT_I2C_SDA_GPIO_Port->BSRR = SOFT_I2C_SDA_Pin;
}

static void sda_low(void)
{
  SOFT_I2C_SDA_GPIO_Port->BSRR = (uint32_t)SOFT_I2C_SDA_Pin << 16U;
}

static bool sda_is_high(void)
{
  return (SOFT_I2C_SDA_GPIO_Port->IDR & SOFT_I2C_SDA_Pin) != 0U;
}

static bool scl_wait_high(void)
{
  scl_release();
  for (uint32_t elapsed = 0U; elapsed < I2C_CLOCK_TIMEOUT_US; ++elapsed) {
    if ((SOFT_I2C_SCL_GPIO_Port->IDR & SOFT_I2C_SCL_Pin) != 0U) return true;
    delay_us(1U);
  }
  return false;
}

static bool start_condition(void)
{
  sda_release();
  if (!scl_wait_high()) return false;
  delay_us(I2C_HALF_PERIOD_US);
  if (!sda_is_high()) return false;
  sda_low();
  delay_us(I2C_HALF_PERIOD_US);
  scl_low();
  delay_us(I2C_HALF_PERIOD_US);
  return true;
}

static void stop_condition(void)
{
  sda_low();
  delay_us(I2C_HALF_PERIOD_US);
  (void)scl_wait_high();
  delay_us(I2C_HALF_PERIOD_US);
  sda_release();
  delay_us(I2C_HALF_PERIOD_US);
}

static bool write_byte(uint8_t value)
{
  for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1U) {
    if ((value & mask) != 0U) sda_release(); else sda_low();
    delay_us(I2C_HALF_PERIOD_US);
    if (!scl_wait_high()) return false;
    delay_us(I2C_HALF_PERIOD_US);
    scl_low();
    delay_us(I2C_HALF_PERIOD_US);
  }

  sda_release();
  delay_us(I2C_HALF_PERIOD_US);
  if (!scl_wait_high()) return false;
  delay_us(I2C_HALF_PERIOD_US);
  const bool acknowledged = !sda_is_high();
  scl_low();
  delay_us(I2C_HALF_PERIOD_US);
  return acknowledged;
}

static bool read_byte(uint8_t *value, bool acknowledge)
{
  *value = 0U;
  sda_release();
  for (uint8_t i = 0U; i < 8U; ++i) {
    *value <<= 1U;
    delay_us(I2C_HALF_PERIOD_US);
    if (!scl_wait_high()) return false;
    delay_us(I2C_HALF_PERIOD_US);
    if (sda_is_high()) *value |= 1U;
    scl_low();
    delay_us(I2C_HALF_PERIOD_US);
  }

  if (acknowledge) sda_low(); else sda_release();
  delay_us(I2C_HALF_PERIOD_US);
  if (!scl_wait_high()) return false;
  delay_us(I2C_HALF_PERIOD_US);
  scl_low();
  sda_release();
  delay_us(I2C_HALF_PERIOD_US);
  return true;
}

void soft_i2c_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0U;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  sda_release();
  scl_release();
  delay_us(I2C_HALF_PERIOD_US);

  /* 从传输中途复位造成的总线占用状态中恢复。 */
  if (!sda_is_high()) {
    for (uint8_t i = 0U; i < 9U && !sda_is_high(); ++i) {
      scl_low(); delay_us(I2C_HALF_PERIOD_US);
      (void)scl_wait_high(); delay_us(I2C_HALF_PERIOD_US);
    }
  }
  stop_condition();
}

bool soft_i2c_mem_read(uint8_t address_7bit, uint8_t reg, uint8_t *data, uint16_t length)
{
  if (data == NULL || length == 0U) return false;
  if (!start_condition()) goto fail;
  if (!write_byte((uint8_t)(address_7bit << 1U))) goto fail;
  if (!write_byte(reg)) goto fail;
  if (!start_condition()) goto fail;
  if (!write_byte((uint8_t)((address_7bit << 1U) | 1U))) goto fail;

  for (uint16_t i = 0U; i < length; ++i) {
    if (!read_byte(&data[i], (i + 1U) < length)) goto fail;
  }
  stop_condition();
  return true;

fail:
  stop_condition();
  return false;
}
