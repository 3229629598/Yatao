#ifndef tca9548a_h
#define tca9548a_h

#include "i2c.h"

#define TCA9548A_ADDR 0x70
#define TCA9548A_WRITE_BIT 0x00
#define TCA9548A_READ_BIT 0x01

#define TCS34725_W_ADDR ((TCS34725_ADDR << 1) | TCA9548A_WRITE_BIT)
#define TCS34725_R_ADDR ((TCS34725_ADDR << 1) | TCA9548A_READ_BIT)

#include "tcs34725.h"

void i2c_my_init(void);
void i2c_rgb_update(void);

#endif
