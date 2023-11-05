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
#include "audio.h"
#include "sync.h"

#include "params.h"

const char *TAG = "v06x";

volatile int bounce_empty_ctr = 0;
volatile int bounce_core = 0;
volatile int filling = 0;

volatile uint32_t frame_no = 0;


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
    audio::allocate_buffers();

    // init scaler task on core 1 to pin scaler to core 1
    scaler::create_pinned_to_core();
    scaler::main_screen_turn_on();

    vTaskDelay(pdMS_TO_TICKS(500));

    audio::create_pinned_to_core();

    v06x::init(scaler_to_emu, scaler::bounce_buf8[0], scaler::bounce_buf8[1]);
    v06x::create_pinned_to_core();
    
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
                scaler::fps, scaler::v06x_fps, (scaler::v06x_frame_cycles - last_frame_cycles) / 50, scaler::frameduration_us);
            last_frame_cycles = scaler::v06x_frame_cycles;
        }
        
        // if (tutu_i == NTUTU) {
        //         printf("0: len=%d line=%d\n", tutu_len_bytes[0], tutu_pos_px[0]/EXAMPLE_LCD_H_RES);
        //         for (int i = 1; i < NTUTU; ++i) printf("%2d: t=%lld line=%d l=%d\n", i, tutu[i] - tutu[i-1], tutu_pos_px[i]/EXAMPLE_LCD_H_RES, tutu_len_bytes[i]);
        //         tutu_i = -1;
        //         putchar('\n');
        // }

    }
}
