#include "bringup.h"

void bringup_init(void)
{
    tcs34725_init();
    HAL_Delay(10);
    HAL_TIM_Base_Start_IT(&htim1);
}

void tim1_loop(void)
{
    tcs34725_loop();
}

void main_loop(void)
{}
