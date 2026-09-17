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
}
