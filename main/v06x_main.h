#ifndef _V06X_MAIN_
#define _V06X_MAIN_

#include "freertos/FreeRTOS.h"

namespace v06x
{
void init(SemaphoreHandle_t sem_request_handle, uint8_t * _buf0, uint8_t * _buf1);
void create_pinned_to_core(void);
void blob_loaded(void);     // sdcard loaded data, suck them into the emulation
}
#endif