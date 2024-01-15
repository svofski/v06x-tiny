/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <cstring>
#include <cmath>
#include <cstdint>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "spibus.h"
#include "AySound.h"
#include "v06x_main.h"
#include "esp_filler.h"
#include "scaler.h"
#include "audio.h"
#include "sync.h"
#include "sdcard.h"

#include "osd.h"

#include "params.h"

const char *TAG = "v06x";

volatile int bounce_empty_ctr = 0;
volatile int bounce_core = 0;
volatile int filling = 0;

volatile uint32_t frame_no = 0;

//bool osd_showing = false;
bool sdcard_busy = false;

SDCard sdcard{};
OSD osd(sdcard);

typedef enum {OSD_NOCHANGE = 0, OSD_SHOW = 1, OSD_HIDE = 2} osd_change_t;
volatile osd_change_t osd_change = OSD_NOCHANGE;

extern "C"
void app_main(void)
{
    syncp::create_primitives();

    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(static_cast<gpio_num_t>(PIN_NUM_BK_LIGHT), LCD_BK_LIGHT_OFF_LEVEL);

    scaler::allocate_buffers();
    audio::allocate_buffers();


    // init scaler task on core 1 to pin scaler to core 1
    scaler::create_pinned_to_core();
    scaler::main_screen_turn_on();
    //scaler::show_osd(&osd);

    vTaskDelay(pdMS_TO_TICKS(500));

    //audio::create_pinned_to_core();   -- hiccups

    // initialize spi bus before keyboard and sdcard
    SPIBus spi_bus{};

    // SPI keyboard
    keyboard::init();

    // let the keyboard finish SPI transaction if we were reset
    keyboard::read_rows();

    // this will also scan the directories for all usable assets
    printf("Delay before mounting SD card\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    sdcard.create_pinned_to_core();

    v06x::init(scaler_to_emu, scaler::bounce_buf8[0], scaler::bounce_buf8[1]);
    v06x::create_pinned_to_core();
    audio::create_pinned_to_core();     // here it seems to work better -- no hiccups

    osd.init(268, 240); // scandoubled osd
    osd.x = 532;
    osd.y = 0;

    osd_change = OSD_NOCHANGE;

    // show/hide OSD when US+RUS is pressed for 1s
    // called from the main emulator task --> delegate to the frame loop
    esp_filler::onosd = []() {
        if (!osd.visible) {
            osd_change = OSD_SHOW;
        }
        else {
            osd_change = OSD_HIDE;
        }
    };

    // callbacks from osd.show()/hide(), all in this task
    osd.onshown = []() {
        scaler::show_osd(&osd);
        keyboard::osd_takeover(true, true);
    };
    osd.onhidden = []() {
        scaler::show_osd(nullptr);
        keyboard::osd_takeover(false);
    };
    osd.onload = [](AssetKind k, int index) {
        sdcard.load_asset(k, index);
    };

    ESP_LOGI(TAG, "After all allocations: remaining DRAM: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "After all allocations: remaining PSRAM: %u\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    int last_frame_cycles = 0;
    while (1) {        
        xSemaphoreGive(sem_gui_ready);
        xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
        frame_no = frame_no + 1;

        switch (osd_change) {
            case OSD_SHOW:
                osd.show();
                break;
            case OSD_HIDE:
                osd.hide();
                break;
            default:
                break;
        }
        osd_change = OSD_NOCHANGE;

        if (osd.visible) {
            osd.frame(frame_no);
        }

        int loaded = 0;
        if (xQueueReceive(sdcard.osd_notify_queue, &loaded, 0)) {
            if (loaded > 0) {
                v06x::blob_loaded(); // blob loaded, pass it on to memory
            }
            else {
                switch(loaded) {
                    case NOTIFY_SDCARD_MOUNTED:
                        printf("sdcard please rescan\n");
                        sdcard.rescan_sdcard();
                        break;
                    case NOTIFY_RESCAN_COMPLETE:
                        printf("sdcard rescan complete\n");
                        break;
                }
            }
        }

        if (frame_no % 50 == 0) {
            #if ENABLE_FRAME_REPORT
            printf("fps=%d v06x_fps=%d cycles=%d frame dur=%lluus\r",
                scaler::fps, scaler::v06x_fps, (esp_filler::v06x_frame_cycles - last_frame_cycles) / 50, scaler::frameduration_us);
            #endif
            last_frame_cycles = esp_filler::v06x_frame_cycles;
        }
    }
}
