/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "AySound.h"
#include "v06x_main.h"
#include "esp_filler.h"
#include "scaler.h"
#include "sync.h"

#include "params.h"

#if WITH_PWM_AUDIO
#include "pwm_audio.h"
#endif

#if WITH_I2S_AUDIO
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#endif

#define CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM 1

const char *TAG = "v06x";

volatile int bounce_empty_ctr = 0;
volatile int bounce_core = 0;
volatile int filling = 0;
volatile int v06x_frame_cycles = 0;

audio_sample_t * audio_pp[AUDIO_NBUFFERS];
uint8_t * ay_pp[AUDIO_NBUFFERS];

volatile uint32_t frame_no = 0;


#if WITH_PWM_AUDIO
IRAM_ATTR
void audio_task(void *unused)
{
    size_t written;

    // PWM Audio Init
    pwm_audio_config_t pac;
    pac.duty_resolution    = LEDC_TIMER_8_BIT;
    pac.gpio_num_left      = SPEAKER_PIN;
    pac.ledc_channel_left  = LEDC_CHANNEL_0;
    pac.gpio_num_right     = -1;
    pac.ledc_channel_right = LEDC_CHANNEL_1;
    pac.ledc_timer_sel     = LEDC_TIMER_0;
    pac.tg_num             = TIMER_GROUP_0;
    pac.timer_num          = TIMER_0;
    pac.ringbuf_len        = /* 1024 * 8;*/ /*2560;*/ 2880;

    pwm_audio_init(&pac);
    pwm_audio_set_param(AUDIO_SAMPLERATE, LEDC_TIMER_8_BIT, 1);
    pwm_audio_start();
    pwm_audio_set_volume(aud_volume);

    while(1) {
        int num, written;
        xQueueReceive(audio_queue, &num, portMAX_DELAY);
        pwm_audio_write(audio_pp[num], AUDIO_SAMPLES_PER_FRAME, &written, 10 / portTICK_PERIOD_MS);
    }
}
#endif


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
            audio_pp[num][i] += ay_pp[num][i] << 6;
        }
        i2s_channel_write(tx_handle, audio_pp[num], AUDIO_SAMPLES_PER_FRAME * AUDIO_SAMPLE_SIZE, &written, 5 / portTICK_PERIOD_MS);
        //printf("audio: buf %d -> %u\n", num, written);
        //printf("ay: falta %d samps\n", AUDIO_SAMPLES_PER_FRAME - esp_filler::ay_bufpos_reg);
        AySound::gen_sound(AUDIO_SAMPLES_PER_FRAME - esp_filler::ay_bufpos_reg, esp_filler::ay_bufpos_reg);
    }
}
#endif

extern "C"
void app_main(void)
{
    syncp::create_primitives();

    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(static_cast<gpio_num_t>(EXAMPLE_PIN_NUM_BK_LIGHT), EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);


    scaler::allocate_buffers();

    for (int i = 0; i < AUDIO_NBUFFERS; ++i) {
        audio_pp[i] = static_cast<audio_sample_t *>(heap_caps_malloc(AUDIO_SAMPLES_PER_FRAME * AUDIO_SAMPLE_SIZE, MALLOC_CAP_INTERNAL));
        assert(audio_pp[i]);

        ay_pp[i] = static_cast<uint8_t *>(heap_caps_malloc(AUDIO_SAMPLES_PER_FRAME, MALLOC_CAP_INTERNAL));
        assert(ay_pp[i]);
    }

    // init scaler task on core 1 to pin scaler to core 1
    scaler::create_pinned_to_core();
    scaler::main_screen_turn_on();

    vTaskDelay(pdMS_TO_TICKS(500));

    xTaskCreatePinnedToCore(&audio_task, "audio task", 1024*4, NULL, configMAX_PRIORITIES - 2, NULL, AUDIO_CORE);

    v06x_init(scaler_to_emu, scaler::bounce_buf8[0], scaler::bounce_buf8[1]);
    xTaskCreatePinnedToCore(&v06x_task, "v06x", 1024*6, NULL, configMAX_PRIORITIES - 1, NULL, EMU_CORE);

    int last_frame_cycles = 0;
    while (1) {        
        xSemaphoreGive(sem_gui_ready);
        xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
        ++frame_no;

        // if (index_d >= NSAMP) {
        //     for (int i = 0; i < NSAMP; ++i) {
        //         printf("%d pos_px=%d len_bytes=%d\n", i, pos_px_d[i], len_bytes_d[i]);
        //     }
        // }
        
        if (frame_no % 50 == 0) {
            printf("fps=%d v06x_fps=%d cycles=%d frame dur=%lluus\n",
                scaler::fps, scaler::v06x_fps, (v06x_frame_cycles - last_frame_cycles) / 50, scaler::frameduration_us);
            last_frame_cycles = v06x_frame_cycles;
        }
        
        // if (tutu_i == NTUTU) {
        //         printf("0: len=%d line=%d\n", tutu_len_bytes[0], tutu_pos_px[0]/EXAMPLE_LCD_H_RES);
        //         for (int i = 1; i < NTUTU; ++i) printf("%2d: t=%lld line=%d l=%d\n", i, tutu[i] - tutu[i-1], tutu_pos_px[i]/EXAMPLE_LCD_H_RES, tutu_len_bytes[i]);
        //         tutu_i = -1;
        //         putchar('\n');
        // }

    }
}
