#pragma once 

#include "freertos/semphr.h"
#include "freertos/queue.h"

enum {
    CMD_EMU_BREAK = 1
};

extern SemaphoreHandle_t sem_vsync_end;
extern SemaphoreHandle_t sem_gui_ready;

// scaler to emulator: request next 6 lines
extern QueueHandle_t scaler_to_emu;
extern QueueHandle_t audio_queue;
extern QueueHandle_t emu_command_queue;

namespace syncp {

void create_primitives(void);

}
