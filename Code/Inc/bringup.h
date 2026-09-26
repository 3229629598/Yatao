#ifndef bringup_h
#define bringup_h

#include "config.h"
//#include "tim.h"
#include "tca9548a.h"
#include "tcs34725.h"
//#include "data_process.h"
#include "ble.h"

void bringup_init(void);
void tim1_loop(void);
void main_loop(void);
void p2ps_stm_write_evt(P2PS_STM_App_Notification_evt_t *pNotification);

#endif
