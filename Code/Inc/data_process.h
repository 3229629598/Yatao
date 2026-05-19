#ifndef data_process_h
#define data_process_h

#include "ux_device_cdc_acm.h"
#include "app_usbx_device.h"
#include "tcs34725.h"
#include "crc.h"

#define usb_tx_header 0x5a

typedef __packed struct
{
    uint8_t header;
    tcs34725_rgb rbg_data[2];
    uint16_t crc_sum;
}usb_tx;

void data_process_init(void);
void data_process_loop(void);

#endif