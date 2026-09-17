#include "bringup.h"

uint32_t last_time1, last_time2;

void bringup_init(void)
{
    i2c_my_init();
//    data_process_init();
}

void tim1_loop(void)
{
//    data_process_loop();
}

void main_loop(void)
{
	if(HAL_GetTick() - last_time1 >= 100) 
	{
		last_time1 = HAL_GetTick();
		i2c_rgb_update(); 
	}
//	if(HAL_GetTick() - last_time2 >= 1000)
//	{
//		last_time2 = HAL_GetTick();
//		HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
//	}
}
