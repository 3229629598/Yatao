#ifndef data_process_h
#define data_process_h

#include "usb.h"
#include "tcs34725.h"
#include "crc.h"
#include "string.h"

#define usb_tx_header 0x5a

typedef struct __attribute__((packed))
{
    uint8_t header;
    tcs34725_rgb rbg_data[2];
    uint16_t crc_sum;
}usb_tx;

void data_process_init(void);
void data_process_loop(void);

#endif