#ifndef tca9548a_h
#define tca9548a_h

#include "i2c.h"
#include "tcs3448.h"

#define TCA9548A_ADDR 0x70

void i2c_my_init(void);
void i2c_rgb_update(void);

#endif
