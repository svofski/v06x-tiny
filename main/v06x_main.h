#ifndef _V06X_MAIN_
#define _V06X_MAIN_

#include "freertos/FreeRTOS.h"

#include "wav.h"
#include "vio.h"

namespace v06x
{
extern IO * io;
extern WavPlayer* tape_player;
void init(SemaphoreHandle_t sem_request_handle, uint8_t * _buf0, uint8_t * _buf1);
void create_pinned_to_core(void);
void blob_loaded(void);     // sdcard loaded data, suck them into the emulation
}
#endif