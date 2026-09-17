#include "tcs34725.h"

// 写寄存器
static void TCS_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t data)
{
    uint8_t buf[2];
    buf[0] = 0x80 | reg;
    buf[1] = data;
    HAL_I2C_Master_Transmit(hi2c, TCS34725_W_ADDR, buf, 2, 10);
}

// 读寄存器
static uint8_t TCS_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg)
{
    uint8_t buf;
    uint8_t cmd = 0x80 | reg;
    HAL_I2C_Master_Transmit(hi2c, TCS34725_W_ADDR, &cmd, 1, 10);
    HAL_I2C_Master_Receive(hi2c, TCS34725_R_ADDR, &buf, 1, 10);
    return buf;
}

// 读原始 16bit RGBC
static void TCS_ReadRGB(I2C_HandleTypeDef *hi2c, uint16_t *R, uint16_t *G, uint16_t *B, uint16_t *C)
{
//    uint8_t l,h;

//    l = TCS_ReadReg(hi2c, 0x14);
//    h = TCS_ReadReg(hi2c, 0x15);
//    *C = (h<<8) | l;

//    l = TCS_ReadReg(hi2c, 0x16);
//    h = TCS_ReadReg(hi2c, 0x17);
//    *R = (h<<8) | l;

//    l = TCS_ReadReg(hi2c, 0x18);
//    h = TCS_ReadReg(hi2c, 0x19);
//    *G = (h<<8) | l;

//    l = TCS_ReadReg(hi2c, 0x1A);
//    h = TCS_ReadReg(hi2c, 0x1B);
//    *B = (h<<8) | l;
	
		uint8_t buf[8];
    uint8_t cmd = 0x80 | 0x14;

    HAL_I2C_Master_Transmit(hi2c, TCS34725_W_ADDR, &cmd, 1, 10);    
    HAL_I2C_Master_Receive(hi2c, TCS34725_R_ADDR, buf, 8, 10);

    *C = (uint16_t)(buf[0] | (buf[1] << 8));
    *R = (uint16_t)(buf[2] | (buf[3] << 8));
    *G = (uint16_t)(buf[4] | (buf[5] << 8));
    *B = (uint16_t)(buf[6] | (buf[7] << 8));
}

tcs34725_rgb rgbdata[4];

// 初始化
void TCS34725_Init(I2C_HandleTypeDef *hi2c)
{
    TCS_WriteReg(hi2c, 0x00, 0x03);
    TCS_WriteReg(hi2c, 0x01, 0xD5);
    TCS_WriteReg(hi2c, 0x0F, 0x02);
}

// 获取标准 RGB888 (0~255)
void TCS34725_GetRGB888(I2C_HandleTypeDef *hi2c, tcs34725_rgb *rgbdata)
{
    uint16_t r, g, b, c;
    float rf, gf, bf, max;

    TCS_ReadRGB(hi2c, &r, &g, &b, &c);

    if(c == 0)
		{ 
			rgbdata->r = 0;
			rgbdata->g = 0;
			rgbdata->b = 0;
			return; 
		}

    rf = (float)r / c;
    gf = (float)g / c;
    bf = (float)b / c;

    max = rf;
    if(gf > max) max = gf;
    if(bf > max) max = bf;

    rgbdata->r = (uint8_t)(rf / max * 255);
    rgbdata->g = (uint8_t)(gf / max * 255);
    rgbdata->b = (uint8_t)(bf / max * 255);
}
