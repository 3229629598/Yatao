#include "tca9548a.h"

HAL_StatusTypeDef TCA9548A_SelectChannel(uint8_t channel, I2C_HandleTypeDef *hi2c)
{
    if(channel > 7)
        return HAL_ERROR;
		
    uint8_t data = 1 << channel;
    return HAL_I2C_Master_Transmit(hi2c, TCA9548A_ADDR << 1, &data, 1, 10);
}

void TCA9548A_DisableAll(void)
{
    uint8_t data = 0x00;

    HAL_I2C_Master_Transmit(&hi2c1, TCA9548A_ADDR << 1, &data, 1, 10);
}

void i2c_my_init()
{
    for(int i = 0; i < 4; i++)
    {
        HAL_StatusTypeDef status = TCA9548A_SelectChannel(i, &hi2c1);
        if(status != HAL_OK)
        {
            Error_Handler();
        }
        TCS34725_Init(&hi2c1);
        HAL_Delay(5);
    }
}

void i2c_rgb_update()
{
    for(int i = 0; i < 4; i++)
    {
        TCA9548A_SelectChannel(i, &hi2c1);
        TCS34725_GetRGB888(&hi2c1, &rgbdata[i]);
    }
}
