#include "imu.h"

#define IMU_READ_RATE 2000 //400Hz min

// LSM6DSR REGISTERS

#define FUNC_CFG_ACCESS 0x01
#define CTRL1_XL        0x10
#define CTRL2_G         0x11  // Gyro Activation Register - Write 0x60 to enable 416hz
#define CTRL3_C         0x12  // Used to set BDU
#define CTRL4_C         0x13
#define CTRL6_C         0x15
#define CTRL8_XL        0x17
#define CTRL9_XL        0x18
#define CTRL10_C        0x19

#define FUNC_MASK   (0b10000000) // Enable FUNC CFG access
#define CTRL1_MASK  (0b10001110) // 1.66kHz, 8G, output first stage filtering
#define CTRL2_MASK  (0b10001100) // 208Hz, 2000dps
#define CTRL3_MASK  (0b01000100) // BDU enabled and Interrupt out active low
#define CTRL4_MASK  (0b00000101) // I2C disable (Check later for LPF for gyro)
#define CTRL6_MASK  (0b00000111) // 12.2 LPF gyro
#define CTRL8_MASK  (0b00000100) //H P_SLOPE_XL_EN
#define CTRL9_MASK  (0x38)
#define CTRL10_MASK (0x38 | 0x4)

#define SPI_READ_BIT 0x80

#define IMU_OUTX_L_G 0x22

uint8_t imu_x[6] = {0};
uint8_t imu_y[6] = {0};
uint8_t imu_z[6] = {0};

uint8_t acc_x[6] = {0};
uint8_t acc_y[6] = {0};
uint8_t acc_z[6] = {0};

auto_init_mutex(imu_mutex);

bool _imu_enabled = false;

void imu_set_enabled(bool enable)
{
  _imu_enabled = enable;
}

// Gets the last 3 IMU datasets and puts them out
// for Nintendo Switch buffer
void imu_buffer_out(uint8_t *output)
{
  uint32_t owner_out;
  while(!mutex_try_enter(&imu_mutex, &owner_out))
  {

  }

  output[0] = acc_y[0];
  output[1] = acc_y[1];

  output[2] = acc_x[0];
  output[3] = acc_x[1];

  output[4] = acc_z[0];
  output[5] = acc_z[1];

  output[6]   = imu_y[0];
  output[7]   = imu_y[1];

  output[8]   = imu_x[0];
  output[9]   = imu_x[1];

  output[10]  = imu_z[0];
  output[11]  = imu_z[1];

  // Group 2

  output[12] = acc_y[2];
  output[13] = acc_y[3];

  output[14] = acc_x[2];
  output[15] = acc_x[3];

  output[16] = acc_z[2];
  output[17] = acc_z[3];

  output[18]   = imu_y[2];
  output[19]   = imu_y[3];

  output[20]   = imu_x[2];
  output[21]   = imu_x[3];

  output[22]  = imu_z[2];
  output[23]  = imu_z[3];

  // Group 3

  output[24] = acc_y[4];
  output[25] = acc_y[5];

  output[26] = acc_x[4];
  output[27] = acc_x[5];

  output[28] = acc_z[4];
  output[29] = acc_z[5];

  output[30]   = imu_y[4];
  output[31]   = imu_y[5];

  output[32]   = imu_x[4];
  output[33]   = imu_x[5];

  output[34]  = imu_z[4];
  output[35]  = imu_z[5];

  mutex_exit(&imu_mutex);
}

void _imu_write_register(const uint8_t reg, const uint8_t data)
{
  gpio_put(PGPIO_IMU0_CS, false);
  const uint8_t dat[2] = {reg, data};
  spi_write_blocking(spi0, dat, 2);
  gpio_put(PGPIO_IMU0_CS, true);
  sleep_ms(2);
}

void imu_init()
{
  _imu_write_register(CTRL1_XL, CTRL1_MASK);
  _imu_write_register(CTRL2_G, CTRL2_MASK);
  _imu_write_register(CTRL3_C, CTRL3_MASK);
  _imu_write_register(CTRL4_C, CTRL4_MASK);
  _imu_write_register(CTRL6_C, CTRL6_MASK);
  _imu_write_register(CTRL8_XL, CTRL8_MASK);
}

bool _imu_update_ready(uint32_t timestamp)
{
  static uint32_t last_time = 0;
  static uint32_t this_time = 0;

  this_time = timestamp;

  // Clear variable
  uint32_t diff = 0;

  // Handle edge case where time has
  // looped around and is now less
  if (this_time < last_time)
  {
    diff = (0xFFFFFFFF - last_time) + this_time;
  }
  else if (this_time > last_time)
  {
    diff = this_time - last_time;
  }
  else
    return false;

  // We want a target IMU rate defined
  if (diff > IMU_READ_RATE)
  {
    // Set the last time
    last_time = this_time;
    return true;
  }
  return false;
}

int16_t _concat_reg_data(uint8_t msb, uint8_t lsb, bool invert)
{
  int16_t out = (int16_t)((msb<<8) | lsb);
  if (invert) return -out;
  return out;
}

uint8_t imu_read_idx = 0;

void imu_reset_idx()
{
  imu_read_idx = 0;
}

void imu_read_test(uint32_t timestamp)
{
  if(_imu_update_ready(timestamp) && _imu_enabled)
  {
    uint8_t i[12] = {0};
    gpio_put(PGPIO_IMU0_CS, false);
    const uint8_t reg = 0x80 | IMU_OUTX_L_G;
    spi_write_blocking(spi0, &reg, 1);
    spi_read_blocking(spi0, 0, &i[0], 12);
    gpio_put(PGPIO_IMU0_CS, true);

    uint32_t owner_out;
    while(!mutex_try_enter(&imu_mutex, &owner_out))
    {

    }

    uint8_t o = imu_read_idx*2;

    imu_x[0+o] = i[0];
    imu_x[1+o] = i[1];
    imu_y[0+o] = i[2];
    imu_y[1+o] = i[3];
    imu_z[0+o] = i[4];
    imu_z[1+o] = i[5];

    acc_x[0+o] = 188;
    acc_x[1+o] = 254;
    acc_y[0+o] = 186;
    acc_y[1+o] = 0;
    acc_z[0+o] = 25;
    acc_z[1+o] = 16;

    if (imu_read_idx<3) imu_read_idx++;

    mutex_exit(&imu_mutex);
  }
}
