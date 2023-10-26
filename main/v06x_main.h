#ifndef _V06X_MAIN_
#define _V06X_MAIN_

#include "freertos/FreeRTOS.h"

void v06x_init(SemaphoreHandle_t sem_request_handle, uint8_t * _buf0, uint8_t * _buf1);
void v06x_task(void *);

#endif