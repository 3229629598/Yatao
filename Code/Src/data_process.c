#include "data_process.h"

usb_tx tx_data;
uint8_t tx_len;


void data_process_init()
{
    tx_len = sizeof(usb_tx);
    tx_data.header = usb_tx_header;
}

void data_process_loop()
{
    memcpy((uint8_t*)&tx_data.rbg_data[0], (uint8_t*)&rgbdata1, sizeof(tcs34725_rgb));
    memcpy((uint8_t*)&tx_data.rbg_data[1], (uint8_t*)&rgbdata2, sizeof(tcs34725_rgb));
    Append_CRC16_Check_Sum((uint8_t*)&tx_data, tx_len);
    TEMPLATE_Transmit((uint8_t*)&tx_data, tx_len);
}
