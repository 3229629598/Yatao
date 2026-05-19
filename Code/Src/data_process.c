#include "data_process.h"

usb_tx tx_data;
uint8_t tx_len;
ULONG actual_length;

UX_SLAVE_CLASS_CDC_ACM cdc_acm_instance;

void data_process_init()
{
    tx_len = sizeof(usb_tx);
}

void data_process_loop()
{
    tx_data.header = usb_tx_header;
    memcpy((uint8_t*)&tx_data.rbg_data[0], (uint8_t*)&rgbdata1, sizeof(tcs34725_rgb));
    memcpy((uint8_t*)&tx_data.rbg_data[1], (uint8_t*)&rgbdata2, sizeof(tcs34725_rgb));
    Append_CRC16_Check_Sum((uint8_t*)&tx_data, tx_len);
    //ux_device_class_cdc_acm_write(&cdc_acm_instance, (uint8_t*)&tx_data, tx_len, &actual_length);
}
