#ifndef tcs34725_h
#define tcs34725_h

#include "i2c.h"
#include "tca9548a.h"

#define TCS34725_ADDR 0x29

#ifndef TCS34725_W_ADDR
#define TCS34725_W_ADDR (TCS34725_ADDR << 1)
#endif

#ifndef TCS34725_R_ADDR
#define TCS34725_R_ADDR (TCS34725_ADDR << 1)
#endif


#define ENABLE_ADDR 0x00 // Value 0x03 : Power on, RGBC enable.
#define ATIME_ADDR 0x01 // Value  INTEG_CYCLES  TIME  MAX COUNT
                        // 0xFF        1       2.4ms     1024
                        // 0xF6        10       24ms     10240
                        // 0xD5        42      101ms     43008
                        // 0xC0        64      154ms     65535
                        // 0x00        256     614ms     65535
#define CONTROL_ADDR 0x0F // Value 0x02 : RGBC GAIN VALUE 16x gain

typedef struct __attribute__((packed))
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} tcs34725_rgb;

void TCS34725_Init(I2C_HandleTypeDef *hi2c);
void TCS34725_GetRGB888(I2C_HandleTypeDef *hi2c, tcs34725_rgb *rgbdata);

extern tcs34725_rgb rgbdata[4];

#endif
