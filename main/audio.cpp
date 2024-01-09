#include <cstdint>
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "params.h"
#include "audio.h"
#include "sync.h"
#if WITH_I2S_AUDIO
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#endif

#include "AySound.h"
#include "esp_filler.h"

namespace audio
{

audio_sample_t * audio_pp[AUDIO_NBUFFERS];
uint8_t * ay_pp[AUDIO_NBUFFERS];


#if WITH_I2S_AUDIO
IRAM_ATTR
void audio_task(void *unused)
{
    i2s_chan_handle_t tx_handle;
    /* Get the default channel configuration by the helper macro.
    * This helper macro is defined in `i2s_common.h` and shared by all the I2S communication modes.
    * It can help to specify the I2S role and port ID */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    /* Allocate a new TX channel and get the handle of this channel */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    /* Setting the configurations, the slot configuration and clock configuration can be generated by the macros
    * These two helper macros are defined in `i2s_std.h` which can only be used in STD mode.
    * They can help to specify the slot and clock configurations for initialization or updating */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLERATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_BCLK,
            .ws = (gpio_num_t)I2S_LRC,
            .dout = (gpio_num_t)I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    /* Initialize the channel */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));

    /* Before writing data, start the TX channel first */
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_LOGI(TAG, "Created I2S channel and started audio task");
    while(1) {
        int num;
        size_t written;
        xQueueReceive(audio_queue, &num, portMAX_DELAY);
        #ifdef CLEAN_BEEP_TEST
        for (int i = 0; i < AUDIO_SAMPLES_PER_FRAME; ++i) {
            audio_pp[num][i] = ((i / 12) & 1) << 13;
        }
        #endif
        for (size_t i = 0; i < AUDIO_SAMPLES_PER_FRAME; ++i) {
            audio_pp[num][i] += ay_pp[num][i] << AUDIO_SCALE_MASTER;
        }
        i2s_channel_write(tx_handle, audio_pp[num], AUDIO_SAMPLES_PER_FRAME * AUDIO_SAMPLE_SIZE, &written, 5 / portTICK_PERIOD_MS);
        //printf("audio: buf %d -> %u\n", num, written);
        //printf("ay: falta %d samps\n", AUDIO_SAMPLES_PER_FRAME - esp_filler::ay_bufpos_reg);
        AySound::gen_sound(AUDIO_SAMPLES_PER_FRAME - esp_filler::ay_bufpos_reg, esp_filler::ay_bufpos_reg);
    }
}
#endif

void allocate_buffers()
{
    for (int i = 0; i < AUDIO_NBUFFERS; ++i) {
        audio_pp[i] = static_cast<audio_sample_t *>(heap_caps_malloc(AUDIO_SAMPLES_PER_FRAME * AUDIO_SAMPLE_SIZE, MALLOC_CAP_INTERNAL));
        assert(audio_pp[i]);

        ay_pp[i] = static_cast<uint8_t *>(heap_caps_malloc(AUDIO_SAMPLES_PER_FRAME, MALLOC_CAP_INTERNAL));
        assert(ay_pp[i]);
    }
}

void create_pinned_to_core()
{
    xTaskCreatePinnedToCore(&audio_task, "audio task", 1024*4, NULL, AUDIO_PRIORITY, NULL, AUDIO_CORE);
}

}