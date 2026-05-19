#include "tcs34725.h"

// 写寄存器
static void TCS_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t data)
{
    uint8_t buf[2];
    buf[0] = 0x80 | reg;
    buf[1] = data;
    HAL_I2C_Master_Transmit(hi2c, TCS34725_ADDR << 1, buf, 2, 100);
}

// 读寄存器
static uint8_t TCS_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
    uint8_t buf;
    uint8_t cmd = 0x80 | reg;
    HAL_I2C_Master_Transmit(hi2c, TCS34725_ADDR << 1, &cmd, 1, 100);
    HAL_I2C_Master_Receive(hi2c, TCS34725_ADDR << 1, &buf, 1, 100);
    return buf;
}

// 读原始 16bit RGBC
static void TCS_ReadRGB(I2C_HandleTypeDef *hi2c, uint16_t *R, uint16_t *G, uint16_t *B, uint16_t *C)
{
    uint8_t l,h;

    l = TCS_ReadReg(hi2c, 0x14);
    h = TCS_ReadReg(hi2c, 0x15);
    *C = (h<<8) | l;

    l = TCS_ReadReg(hi2c, 0x16);
    h = TCS_ReadReg(hi2c, 0x17);
    *R = (h<<8) | l;

    l = TCS_ReadReg(hi2c, 0x18);
    h = TCS_ReadReg(hi2c, 0x19);
    *G = (h<<8) | l;

    l = TCS_ReadReg(hi2c, 0x1A);
    h = TCS_ReadReg(hi2c, 0x1B);
    *B = (h<<8) | l;
}

// 初始化
void TCS34725_Init(I2C_HandleTypeDef *hi2c)
{
    TCS_WriteReg(hi2c, 0x00, 0x03);
    TCS_WriteReg(hi2c, 0x01, 0xD5);
    TCS_WriteReg(hi2c, 0x0F, 0x02);
}

// 获取标准 RGB888 (0~255)
void TCS34725_GetRGB888(I2C_HandleTypeDef *hi2c, uint8_t *R, uint8_t *G, uint8_t *B)
{
    uint16_t r, g, b, c;
    float rf, gf, bf, max;

    TCS_ReadRGB(hi2c, &r, &g, &b, &c);

    if(c == 0) { *R=0; *G=0; *B=0; return; }

    rf = (float)r / c;
    gf = (float)g / c;
    bf = (float)b / c;

    max = rf;
    if(gf > max) max = gf;
    if(bf > max) max = bf;

    *R = (uint8_t)(rf / max * 255);
    *G = (uint8_t)(gf / max * 255);
    *B = (uint8_t)(bf / max * 255);
}

tcs34725_rgb rgbdata1, rgbdata2;

void tcs34725_init()
{
    TCS34725_Init(&hi2c1);
    TCS34725_Init(&hi2c2);
}

void tcs34725_loop()
{
    TCS34725_GetRGB888(&hi2c1, &rgbdata1.r, &rgbdata1.g, &rgbdata1.b);
    TCS34725_GetRGB888(&hi2c2, &rgbdata2.r, &rgbdata2.g, &rgbdata2.b);
}
