#ifndef data_process_h
#define data_process_h

#include "tcs34725.h"
#include "tcs3448.h"
#include "ble.h"
#include "config.h"
#include "string.h"

#define usb_tx_header 0x5a

typedef struct __attribute__((packed))
{
    uint8_t header;
    uint16_t crc_sum;
}usb_tx;

void data_process_init(void);

/* 把 4 颗传感器的光谱数据打包成 4 条通知（一条一颗）发出去。
 * 由 bringup.c 在收到 PC 的 0x02 命令后按 10Hz 调用。
 * 协议栈忙时本轮剩下的直接跳过，不做排队重发。 */
void data_process_notify_spectral(void);

#endif
