#include "bringup.h"

uint8_t led_enable = 0, ble_send_enable = 0;
static uint32_t last_send_time = 0;     /* 上次上报 4 颗数据的时刻 */
static const uint32_t OtaTagPadding __attribute__((section("OTA_PAD"), used)) = 0xFFFFFFFFu;

void bringup_init(void)
{
    i2c_my_init();
//    data_process_init();
}

void tim1_loop(void)
{
}

void main_loop(void)
{
	/* 光谱采集：非阻塞状态机，内部自己按 100ms 一轮的节奏跑；
	 * 每次进来只做一两笔很短的 I2C 传输，不会卡住 BLE 协议栈 */
	i2c_rgb_update();

	/* ---- 10Hz 连续上报 ----
	 * 收到 0x02 后：每 100ms 把 4 颗传感器的数据发一轮（4 条通知）
	 * 收到 0x03 后：停止
	 * 协议栈忙时那一轮直接跳过，不排队、不补发 */
	if(ble_send_enable && (HAL_GetTick() - last_send_time >= 100))
	{
		last_send_time = HAL_GetTick();
		data_process_notify_spectral();
	}
}

void p2ps_stm_write_evt(P2PS_STM_App_Notification_evt_t *pNotification)
{
	if(pNotification->DataTransfered.pPayload[0] == 0x00)
	{
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
		led_enable = 0;
	}
	else if(pNotification->DataTransfered.pPayload[0] == 0x01)
	{
		// last_time2 = HAL_GetTick();
		HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
		led_enable = 1;
	}
	else if(pNotification->DataTransfered.pPayload[0] == 0x02)
	{
		/* 开始 10Hz 连续上报：先立刻发一组，之后由 main_loop 每 100ms 发一组 */
		if(ble_send_enable == 0)
		{
			ble_send_enable = 1;
			last_send_time = HAL_GetTick();
			data_process_notify_spectral();
		}
	}
	else if(pNotification->DataTransfered.pPayload[0] == 0x03)
	{
		/* 停止连续上报：已经排队的通知会发完，之后不再新发 */
		ble_send_enable = 0;
	}
}
