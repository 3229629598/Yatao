#include "bringup.h"

uint32_t last_time1, last_time2;
uint8_t ble_data[2];
uint8_t led_enable = 0;
static const uint32_t OtaTagPadding __attribute__((section("OTA_PAD"), used)) = 0xFFFFFFFFu;

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
	if(HAL_GetTick() - last_time2 >= 2000)
	{
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
		led_enable = 0;
	}
}

void p2ps_stm_write_evt(P2PS_STM_App_Notification_evt_t *pNotification)
{
	if(pNotification->DataTransfered.pPayload[0] == 0x01 && led_enable == 0)
	{
		last_time2 = HAL_GetTick();
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
		led_enable = 1;
	}
	else if(pNotification->DataTransfered.pPayload[0] == 0x02)
	{
		for(int i = 0; i < 12; i++)
		{
			ble_data[0] = i;
			if(i%3 == 0)
				ble_data[1] = rgbdata[i/3].r;
			else if(i%3 == 1)
				ble_data[1] = rgbdata[i/3].g;
			else if(i%3 == 2)
				ble_data[1] = rgbdata[i/3].b;
			P2PS_STM_App_Update_Char(P2P_NOTIFY_CHAR_UUID, ble_data);
		}
	}
}
