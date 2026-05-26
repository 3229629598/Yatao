#ifndef bringup_h
#define bringup_h

#include "config.h"
#include "tim.h"
#include "tcs34725.h"
#include "data_process.h"

void bringup_init(void);
void tim1_loop(void);
void main_loop(void);

#endif