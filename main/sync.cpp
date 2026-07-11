#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_log.h"

#include "params.h"
#include "sync.h"

SemaphoreHandle_t sem_vsync_end;
SemaphoreHandle_t sem_gui_ready;

// scaler to emulator: request next 6 lines
QueueHandle_t scaler_to_emu;
#if !(I2S_NOQUEUE)
QueueHandle_t audio_queue;
#endif
QueueHandle_t emu_command_queue;

namespace syncp {

void create_primitives()
{
    ESP_LOGI(TAG, "Create semaphores");
    sem_vsync_end = xSemaphoreCreateBinary();
    assert(sem_vsync_end);
    sem_gui_ready = xSemaphoreCreateBinary();
    assert(sem_gui_ready);

    scaler_to_emu = xQueueCreate(2, sizeof(int));
#if !(I2S_NOQUEUE)
    audio_queue = xQueueCreate(AUDIO_NBUFFERS, sizeof(audio_queue_item_t));
#endif
    emu_command_queue = xQueueCreate(1, sizeof(int));
}

}
