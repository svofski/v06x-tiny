#include <cstdint>
#include <cstdio>
#include <cstring>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include <soc/lcd_cam_reg.h>
#include <soc/lcd_cam_struct.h>

#include "params.h"
#include "scaler.h"
#include "sync.h"
#include "esp_filler.h"
#include "osd.h"

#define _C8TO16(c)   (((((c) & 7) * 4) << 11) | (((((c) >> 3) & 7) * 8) << 5) | ((((c) >> 6) & 3) * 8))

extern bool sdcard_busy;

IRAM_ATTR
static inline uint16_t C8TO16(uint8_t c) 
{
    return _C8TO16(c);
}

#define DITHER_FRAMES 1

extern QueueHandle_t scaler_to_emu;

namespace scaler
{

// not used
typedef struct user_context_ {
} user_context_t;

user_context_t user_context;


uint32_t read_buffer_index;
uint8_t * bounce_buf8[2];

//uint16_t** osd_buf16;
OSD * osd;
OSD * set_osd = nullptr;
bool  set_osd_pls = false;

volatile int framecount = 0;
volatile uint64_t lastframe_us = 0;
volatile uint64_t frameduration_us = 0;

volatile int fps = 0;
volatile int v06x_fps = 0;

static void create_lcd_driver_task(void *pvParameter);
static bool example_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data);

void allocate_buffers()
{

    bounce_buf8[0] = static_cast<uint8_t *>(heap_caps_malloc(BUFCOLUMNS * 6 * sizeof(uint8_t) + 32, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT)) + 6; // alignment offset for main screen area
    assert(bounce_buf8[0]);
    bounce_buf8[1] = static_cast<uint8_t *>(heap_caps_malloc(BUFCOLUMNS * 6 * sizeof(uint8_t) + 32, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT)) + 6; // alignment offset for main screen area
    assert(bounce_buf8[1]);

    memset(bounce_buf8[0], 0, BUFCOLUMNS * 6 * sizeof(uint8_t));
    memset(bounce_buf8[1], 0, BUFCOLUMNS * 6 * sizeof(uint8_t));
    ESP_LOGI(TAG, "Created frame buffers: buf8[0]=%p buf8[1]=%p", bounce_buf8[0], bounce_buf8[1]);
}

void create_pinned_to_core()
{
    xTaskCreatePinnedToCore(&create_lcd_driver_task, "scaler_task", 1024*4, NULL, SCALER_PRIORITY, NULL, SCALER_CORE);
    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
}

void main_screen_turn_on()
{
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(static_cast<gpio_num_t>(PIN_NUM_BK_LIGHT), LCD_BK_LIGHT_ON_LEVEL);
}

static void tick_1s(void *arg)
{
    fps = framecount;
    framecount = 0;  

    v06x_fps = esp_filler::v06x_framecount;
    esp_filler::v06x_framecount = 0;
}

#if 0
// fill one block with scaling up
// scaler 5/4 horizontal, 8/5 vertical, or 4x5 -> 6x8
// bgr233 532x300 -> rgb565 798x480
static void IRAM_ATTR 
fillcolumn_h54(uint16_t * col, uint32_t * src)
{
    int ofs = 0;

    uint32_t s4_0 = src[0];
    uint32_t s4_1 = src[BUFCOLUMNS * 1 / 4];
    uint32_t s4_2 = src[BUFCOLUMNS * 2 / 4];
    uint32_t s4_3 = src[BUFCOLUMNS * 3 / 4];
    uint32_t s4_4 = src[BUFCOLUMNS * 4 / 4];

    uint16_t c16_1;
    uint16_t c16_2;
    uint16_t c16_3;
    uint16_t c16_4;

    c16_1 = C8TO16(s4_0 >> 0);
    c16_2 = C8TO16(s4_0 >> 8);
    c16_3 = C8TO16(s4_0 >> 16);
    c16_4 = C8TO16(s4_0 >> 24);
        // x = 0, 0.6, 1.3, 2.0, 2.6, 3.3
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 0
    c16_1 = C8TO16(s4_1 >> 0);
    c16_2 = C8TO16(s4_1 >> 8);
    c16_3 = C8TO16(s4_1 >> 16);
    c16_4 = C8TO16(s4_1 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 0.625
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 1.25
    c16_1 = C8TO16(s4_2 >> 0);
    c16_2 = C8TO16(s4_2 >> 8);
    c16_3 = C8TO16(s4_2 >> 16);
    c16_4 = C8TO16(s4_2 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 1.875
    c16_1 = C8TO16(s4_3 >> 0);
    c16_2 = C8TO16(s4_3 >> 8);
    c16_3 = C8TO16(s4_3 >> 16);
    c16_4 = C8TO16(s4_3 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 2.5
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 3.125
    c16_1 = C8TO16(s4_4 >> 0);
    c16_2 = C8TO16(s4_4 >> 8);
    c16_3 = C8TO16(s4_4 >> 16);
    c16_4 = C8TO16(s4_4 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 3.75
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // + 4.375
}
#endif 
// fill one block with scaling up
// scaler 5/4 horizontal, 8/5 vertical, or 4x5 -> 6x8
// bgr233 532x300 -> rgb565 798x480
static void IRAM_ATTR 
fillcolumn_h54_v106(uint16_t * col, uint32_t * src)
{
    int ofs = 0;

    // column: [0.0, 0.6, 1.2, 1.8, 2.4, 3.0, 3.6, 4.2, 4.8, 5.4]
    uint32_t s4_0 = src[0];
    uint32_t s4_1 = src[BUFCOLUMNS * 1 / 4];
    uint32_t s4_2 = src[BUFCOLUMNS * 2 / 4];
    uint32_t s4_3 = src[BUFCOLUMNS * 3 / 4];
    uint32_t s4_4 = src[BUFCOLUMNS * 4 / 4];
    uint32_t s4_5 = src[BUFCOLUMNS * 5 / 4];

    uint16_t c16_1;
    uint16_t c16_2;
    uint16_t c16_3;
    uint16_t c16_4;

    // y + 0
    c16_1 = C8TO16(s4_0 >> 0);      
    c16_2 = C8TO16(s4_0 >> 8);
    c16_3 = C8TO16(s4_0 >> 16);
    c16_4 = C8TO16(s4_0 >> 24);
        // x = 0, 0.6, 1.3, 2.0, 2.6, 3.3
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 1
    // y + 0.6, y + 1.2
    c16_1 = C8TO16(s4_1 >> 0);
    c16_2 = C8TO16(s4_1 >> 8);
    c16_3 = C8TO16(s4_1 >> 16);
    c16_4 = C8TO16(s4_1 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 2
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 3
    // y + 1.8, y + 2.4
    c16_1 = C8TO16(s4_2 >> 0);
    c16_2 = C8TO16(s4_2 >> 8);
    c16_3 = C8TO16(s4_2 >> 16);
    c16_4 = C8TO16(s4_2 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 4
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 5
    // y = 3.0
    c16_1 = C8TO16(s4_3 >> 0);
    c16_2 = C8TO16(s4_3 >> 8);
    c16_3 = C8TO16(s4_3 >> 16);
    c16_4 = C8TO16(s4_3 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 6
    // y + 3.6, y + 4.2
    c16_1 = C8TO16(s4_4 >> 0);
    c16_2 = C8TO16(s4_4 >> 8);
    c16_3 = C8TO16(s4_4 >> 16);
    c16_4 = C8TO16(s4_4 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 7
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 8
    // y + 4.8, y + 5.4
    c16_1 = C8TO16(s4_5 >> 0);
    c16_2 = C8TO16(s4_5 >> 8);
    c16_3 = C8TO16(s4_5 >> 16);
    c16_4 = C8TO16(s4_5 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 9
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        //ofs += LCD_H_RES;  // dst_line = 10
}

// hscale 1:1
static void IRAM_ATTR 
fillcolumn_h11_v106(uint16_t * col, uint32_t * src)
{
    int ofs = 0;

    // column: [0.0, 0.6, 1.2, 1.8, 2.4, 3.0, 3.6, 4.2, 4.8, 5.4]
    uint32_t s4_0 = src[0];
    uint32_t s4_1 = src[BUFCOLUMNS * 1 / 4];
    uint32_t s4_2 = src[BUFCOLUMNS * 2 / 4];
    uint32_t s4_3 = src[BUFCOLUMNS * 3 / 4];
    uint32_t s4_4 = src[BUFCOLUMNS * 4 / 4];
    uint32_t s4_5 = src[BUFCOLUMNS * 5 / 4];

    uint16_t c16_1;
    uint16_t c16_2;
    uint16_t c16_3;
    uint16_t c16_4;

    // y + 0
    c16_1 = C8TO16(s4_0 >> 0);      
    c16_2 = C8TO16(s4_0 >> 8);
    c16_3 = C8TO16(s4_0 >> 16);
    c16_4 = C8TO16(s4_0 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 1
    // y + 0.6, y + 1.2
    c16_1 = C8TO16(s4_1 >> 0);
    c16_2 = C8TO16(s4_1 >> 8);
    c16_3 = C8TO16(s4_1 >> 16);
    c16_4 = C8TO16(s4_1 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 2
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 3
    // y + 1.8, y + 2.4
    c16_1 = C8TO16(s4_2 >> 0);
    c16_2 = C8TO16(s4_2 >> 8);
    c16_3 = C8TO16(s4_2 >> 16);
    c16_4 = C8TO16(s4_2 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 4
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 5
    // y = 3.0
    c16_1 = C8TO16(s4_3 >> 0);
    c16_2 = C8TO16(s4_3 >> 8);
    c16_3 = C8TO16(s4_3 >> 16);
    c16_4 = C8TO16(s4_3 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 6
    // y + 3.6, y + 4.2
    c16_1 = C8TO16(s4_4 >> 0);
    c16_2 = C8TO16(s4_4 >> 8);
    c16_3 = C8TO16(s4_4 >> 16);
    c16_4 = C8TO16(s4_4 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 7
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 8
    // y + 4.8, y + 5.4
    c16_1 = C8TO16(s4_5 >> 0);
    c16_2 = C8TO16(s4_5 >> 8);
    c16_3 = C8TO16(s4_5 >> 16);
    c16_4 = C8TO16(s4_5 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 9
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4;
        //ofs += LCD_H_RES;  // dst_line = 10
}


// fill one block with scaling up
// scaler 5/4 horizontal, 8/5 vertical, or 4x5 -> 6x8
// bgr233 532x300 -> rgb565 798x480
static void IRAM_ATTR 
fillcolumn_h54_v106_dither_a(uint16_t * col, const uint32_t * src)
{
    int ofs = 0;

    // column: [0.0, 0.6, 1.2, 1.8, 2.4, 3.0, 3.6, 4.2, 4.8, 5.4]
    uint32_t s4_0 = src[0];
    uint32_t s4_1 = src[BUFCOLUMNS * 1 / 4];
    uint32_t s4_2 = src[BUFCOLUMNS * 2 / 4];
    uint32_t s4_3 = src[BUFCOLUMNS * 3 / 4];
    uint32_t s4_4 = src[BUFCOLUMNS * 4 / 4];
    uint32_t s4_5 = src[BUFCOLUMNS * 5 / 4];

    uint16_t c16_1;
    uint16_t c16_2;
    uint16_t c16_3;
    uint16_t c16_4;

    // y + 0
    c16_1 = C8TO16(s4_0 >> 0);      
    c16_2 = C8TO16(s4_0 >> 8);
    c16_3 = C8TO16(s4_0 >> 16);
    c16_4 = C8TO16(s4_0 >> 24);
        // x = 0, 0.6, 1.3, 2.0, 2.6, 3.3
        col[ofs] = c16_1; col[ofs+1] = c16_1; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 1
    // y + 0.6, y + 1.2
    c16_1 = C8TO16(s4_1 >> 0);
    c16_2 = C8TO16(s4_1 >> 8);
    c16_3 = C8TO16(s4_1 >> 16);
    c16_4 = C8TO16(s4_1 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 2
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 3
    // y + 1.8, y + 2.4
    c16_1 = C8TO16(s4_2 >> 0);
    c16_2 = C8TO16(s4_2 >> 8);
    c16_3 = C8TO16(s4_2 >> 16);
    c16_4 = C8TO16(s4_2 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 4
        col[ofs] = c16_1; col[ofs+1] = c16_1; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 5
    // y = 3.0
    c16_1 = C8TO16(s4_3 >> 0);
    c16_2 = C8TO16(s4_3 >> 8);
    c16_3 = C8TO16(s4_3 >> 16);
    c16_4 = C8TO16(s4_3 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 6
    // y + 3.6, y + 4.2
    c16_1 = C8TO16(s4_4 >> 0);
    c16_2 = C8TO16(s4_4 >> 8);
    c16_3 = C8TO16(s4_4 >> 16);
    c16_4 = C8TO16(s4_4 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 7
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 8
    // y + 4.8, y + 5.4
    c16_1 = C8TO16(s4_5 >> 0);
    c16_2 = C8TO16(s4_5 >> 8);
    c16_3 = C8TO16(s4_5 >> 16);
    c16_4 = C8TO16(s4_5 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_1; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 9
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4; col[ofs+4] = c16_4;
        //ofs += LCD_H_RES;  // dst_line = 10
}

static void IRAM_ATTR 
fillcolumn_h54_v106_dither_b(uint16_t * col, uint32_t * src)
{
    int ofs = 0;

    // column: [0.0, 0.6, 1.2, 1.8, 2.4, 3.0, 3.6, 4.2, 4.8, 5.4]
    uint32_t s4_0 = src[0];
    uint32_t s4_1 = src[BUFCOLUMNS * 1 / 4];
    uint32_t s4_2 = src[BUFCOLUMNS * 2 / 4];
    uint32_t s4_3 = src[BUFCOLUMNS * 3 / 4];
    uint32_t s4_4 = src[BUFCOLUMNS * 4 / 4];
    uint32_t s4_5 = src[BUFCOLUMNS * 5 / 4];

    uint16_t c16_1;
    uint16_t c16_2;
    uint16_t c16_3;
    uint16_t c16_4;

    // y + 0
    c16_1 = C8TO16(s4_0 >> 0);      
    c16_2 = C8TO16(s4_0 >> 8);
    c16_3 = C8TO16(s4_0 >> 16);
    c16_4 = C8TO16(s4_0 >> 24);
        // x = 0, 0.6, 1.3, 2.0, 2.6, 3.3
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 1
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 2
    // y + 0.6, y + 1.2
    c16_1 = C8TO16(s4_1 >> 0);
    c16_2 = C8TO16(s4_1 >> 8);
    c16_3 = C8TO16(s4_1 >> 16);
    c16_4 = C8TO16(s4_1 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_1; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;   // dst_line = 3
    // y + 1.8, y + 2.4
    c16_1 = C8TO16(s4_2 >> 0);
    c16_2 = C8TO16(s4_2 >> 8);
    c16_3 = C8TO16(s4_2 >> 16);
    c16_4 = C8TO16(s4_2 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 4
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 5
    // y = 3.0
    c16_1 = C8TO16(s4_3 >> 0);
    c16_2 = C8TO16(s4_3 >> 8);
    c16_3 = C8TO16(s4_3 >> 16);
    c16_4 = C8TO16(s4_3 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 6
        col[ofs] = c16_1; col[ofs+1] = c16_1; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 7
    // y + 3.6, y + 4.2
    c16_1 = C8TO16(s4_4 >> 0);
    c16_2 = C8TO16(s4_4 >> 8);
    c16_3 = C8TO16(s4_4 >> 16);
    c16_4 = C8TO16(s4_4 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 8
    // y + 4.8, y + 5.4
    c16_1 = C8TO16(s4_5 >> 0);
    c16_2 = C8TO16(s4_5 >> 8);
    c16_3 = C8TO16(s4_5 >> 16);
    c16_4 = C8TO16(s4_5 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += LCD_H_RES;  // dst_line = 9
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_3; col[ofs+3] = c16_4; col[ofs+4] = c16_4;
}

constexpr int NTUTU = 150;
static int tutu_i = -1;
static uint64_t tutu[NTUTU];
static int tutu_pos_px[NTUTU];
static int tutu_len_bytes[NTUTU];
static int tutu_frm = 0;
static int tutu_frm_reg = 0;

static void IRAM_ATTR
copy_osd(uint16_t * bounce16, int lcd_y)
{
    OSD::Color** osd_srcbuf = osd->framebuffer();
    int osdx = osd->x;
    int osdy_first = osd->y;
    int osdy_last = osd->y + (osd->height << 1);
    int osd_w = osd->width;

    for (int i = 0; i < BOUNCE_NLINES; i += 2) // fill 10 lines, doubling
    {
        if (lcd_y + i >= osdy_first && lcd_y + i < osdy_last)
        {
            // convert 8-bit bgr233 osd buffer to double-scanned native 16-bit rgb565
            uint16_t *dst = bounce16 + i * LCD_H_RES + osdx;
            uint16_t *dst2 = dst + LCD_H_RES; // scandouble of the go
            uint8_t *src = osd_srcbuf[(lcd_y + i - osdy_first) >> 1];
            for (int j = 0; j < osd_w; j += 2)
            {
                uint8_t c1 = *src++;
                uint8_t c2 = *src++;
                uint16_t c16_1 = C8TO16(c1);
                uint16_t c16_2 = C8TO16(c2);
                *dst++ = c16_1;
                *dst++ = c16_2;
                *dst2++ = c16_1;
                *dst2++ = c16_2;
            }
        }
    }
}

static bool IRAM_ATTR 
on_bounce_empty_event(esp_lcd_panel_handle_t panel, void *bounce_buf, int pos_px, int len_bytes, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xQueueSendFromISR(scaler_to_emu, &pos_px, &high_task_awoken);

#if TUTU
    if (pos_px == 0) {
        read_buffer_index = 0;
        if (tutu_i == -1) tutu_i = 0;
        tutu_frm_reg = tutu_frm;
        tutu_frm = 0;
    }
    if (tutu_i >= 0 && tutu_i < NTUTU) {
            tutu[tutu_i] = esp_timer_get_time();
            tutu_pos_px[tutu_i] = pos_px;
            tutu_len_bytes[tutu_i] = len_bytes;
            ++tutu_i;
    }
    tutu_frm++;
#endif
    uint8_t * buf8 = bounce_buf8[read_buffer_index];
    uint16_t * bounce16 = (uint16_t *)bounce_buf;

    int border_w = (LCD_H_RES - BUFCOLUMNS * 5/4) / 2;

    uint8_t * bbuf = static_cast<uint8_t *>(bounce_buf);

    if (!osd) {
        for (int i = 0; i < BOUNCE_NLINES; ++i)
        {
#ifdef DEBUG_BOUNCE_BUFFERS
            memset(bbuf, 0xff && read_buffer_index, border_w << 1);
            if (pos_px == 0)
                memset(bbuf, 0xffff, 16);
#else
            memset(bbuf, 0, border_w << 1);
#endif
            memset(bbuf + 2 * BUFCOLUMNS * 5 / 4 + (border_w << 1), 0, (border_w << 1) + 2); // extra pixel because border_w is odd
            bbuf += LCD_H_RES * 2;
        }

#if DITHER_FRAMES
        if ((framecount & 1) == 0)
        {
            for (int dst_x = border_w, src_x = 0; dst_x + 5 < LCD_H_RES - border_w; dst_x += 5, src_x += 4)
            {
                fillcolumn_h54_v106_dither_a(bounce16 + dst_x, reinterpret_cast<uint32_t *>(buf8 + src_x));
            }
        }
        else
        {
            for (int dst_x = border_w, src_x = 0; dst_x + 5 < LCD_H_RES - border_w; dst_x += 5, src_x += 4)
            {
                fillcolumn_h54_v106_dither_b(bounce16 + dst_x, reinterpret_cast<uint32_t *>(buf8 + src_x));
            }
        }
#else
        for (int dst_x = border_w, src_x = 0; dst_x + 5 < LCD_H_RES - border_w; dst_x += 5, src_x += 4)
        {
            fillcolumn_h54_v106(bounce16 + dst_x, reinterpret_cast<uint32_t *>(buf8 + src_x));
        }
#endif
    }

    else {
        // copy v06c framebuffer with 1:1 horz scale
        for (int dst_x = 0, src_x = 0; dst_x + 4 <= BUFCOLUMNS; dst_x += 4, src_x += 4) {
            fillcolumn_h11_v106(bounce16 + dst_x, reinterpret_cast<uint32_t *>(buf8 + src_x));
        }

        // copy osd buffer with 2x vert scale
        int lcd_y = pos_px / LCD_H_RES;
        assert(lcd_y <= 480-10);
        if (!sdcard_busy)
            copy_osd(bounce16, lcd_y);
    }

    // flip buffers for the next time
    read_buffer_index ^= 1;

    return true;
}

void create_lcd_driver_task(void *pvParameter)
{
    xSemaphoreTake(sem_gui_ready, portMAX_DELAY);

    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            // The following parameters should refer to LCD spec
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .hsync_idle_low = 1,
                .vsync_idle_low = 1,
                .de_idle_high = 0,
                .pclk_active_neg = true,
                .pclk_idle_high = 0,
            }
        },
        .data_width = 16, // RGB565 in parallel mode, thus 16bit in width
        .num_fbs = LCD_NUM_FB,
        .bounce_buffer_size_px = BOUNCE_NLINES * LCD_H_RES,
        .sram_trans_align = 8,
        .psram_trans_align = 64,
        .hsync_gpio_num = PIN_NUM_HSYNC,
        .vsync_gpio_num = PIN_NUM_VSYNC,
        .de_gpio_num = PIN_NUM_DE,
        .pclk_gpio_num = PIN_NUM_PCLK,
        .disp_gpio_num = PIN_NUM_DISP_EN,
        .data_gpio_nums = {
            PIN_NUM_DATA0,
            PIN_NUM_DATA1,
            PIN_NUM_DATA2,
            PIN_NUM_DATA3,
            PIN_NUM_DATA4,
            PIN_NUM_DATA5,
            PIN_NUM_DATA6,
            PIN_NUM_DATA7,
            PIN_NUM_DATA8,
            PIN_NUM_DATA9,
            PIN_NUM_DATA10,
            PIN_NUM_DATA11,
            PIN_NUM_DATA12,
            PIN_NUM_DATA13,
            PIN_NUM_DATA14,
            PIN_NUM_DATA15,
        },
        .flags = {
            .refresh_on_demand = false,
            .fb_in_psram = true, // allocate frame buffer in PSRAM
            .no_fb = true,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    ESP_LOGI(TAG, "Initialize RGB LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    LCD_CAM.lcd_ctrl2.lcd_vsync_idle_pol = 0;
    LCD_CAM.lcd_ctrl2.lcd_hsync_idle_pol = 0;

    ESP_LOGI(TAG, "Install 1s tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t tick_timer_args = {
        .callback = &tick_1s,
        .name = "tick 1s"
    };
    esp_timer_handle_t tick_timer_1s = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&tick_timer_args, &tick_timer_1s));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer_1s, 1000 * 1000));

    ESP_LOGI(TAG, "Register event callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = example_on_vsync_event,
        .on_bounce_empty = on_bounce_empty_event,
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, &user_context));

    xSemaphoreGive(sem_vsync_end);
    vTaskDelete(NULL);
}

static bool example_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    frameduration_us = esp_timer_get_time() - lastframe_us;
    lastframe_us = esp_timer_get_time();

    if (set_osd_pls) {
        set_osd_pls = false;
        osd = set_osd;
    }

    BaseType_t high_task_awoken = pdFALSE;
    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE) {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }
    framecount++;
    return high_task_awoken == pdTRUE;
}

void show_osd(OSD * _osd)
{
    //osd = _osd;
    set_osd = _osd;
    set_osd_pls = true;
}

}
