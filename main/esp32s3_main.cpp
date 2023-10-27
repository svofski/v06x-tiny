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
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include <soc/lcd_cam_reg.h>
#include <soc/lcd_cam_struct.h>

#include "v06x_main.h"


#define SCALER_CORE 1
#define EMU_CORE 0

#define CONFIG_EXAMPLE_AVOID_TEAR_EFFECT_WITH_SEM 1
#define CONFIG_EXAMPLE_USE_BOUNCE_BUFFER 1
#define CONFIG_BOUNCE_ONLY 1
#define BOUNCE_NLINES 10 // 288 * 10/6: scale up 6 lines to 10
#define SCALE85 1

const char *TAG = "v06x";

//#define TFT_BL 2

// Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
//     40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
//     45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
//     5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
//     8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */,
//     0 /* hsync_polarity */, 8 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 8 /* hsync_back_porch */,
//     0 /* vsync_polarity */, 8 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 8 /* vsync_back_porch */);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     20500000 // edging at 26500000+ 64+ fps // 20500000 50hz
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT       2
#define EXAMPLE_PIN_NUM_HSYNC          39
#define EXAMPLE_PIN_NUM_VSYNC          41
#define EXAMPLE_PIN_NUM_DE             40
#define EXAMPLE_PIN_NUM_PCLK           42

#define EXAMPLE_PIN_NUM_DATA0          8  // B0
#define EXAMPLE_PIN_NUM_DATA1          3  // B1
#define EXAMPLE_PIN_NUM_DATA2          46 // B2
#define EXAMPLE_PIN_NUM_DATA3          9  // B3
#define EXAMPLE_PIN_NUM_DATA4          1  // B4

#define EXAMPLE_PIN_NUM_DATA5          5  // G0
#define EXAMPLE_PIN_NUM_DATA6          6  // G1
#define EXAMPLE_PIN_NUM_DATA7          7  // G2
#define EXAMPLE_PIN_NUM_DATA8          15 // G3
#define EXAMPLE_PIN_NUM_DATA9          16 // G4
#define EXAMPLE_PIN_NUM_DATA10         4  // G5

#define EXAMPLE_PIN_NUM_DATA11         45 // R0
#define EXAMPLE_PIN_NUM_DATA12         48 // R1
#define EXAMPLE_PIN_NUM_DATA13         47 // R2
#define EXAMPLE_PIN_NUM_DATA14         21 // R3
#define EXAMPLE_PIN_NUM_DATA15         14 // R4
#define EXAMPLE_PIN_NUM_DISP_EN        -1

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES              800
#define EXAMPLE_LCD_V_RES              480

#if CONFIG_EXAMPLE_DOUBLE_FB
#define EXAMPLE_LCD_NUM_FB             2
#else
#define EXAMPLE_LCD_NUM_FB             1
#endif // CONFIG_EXAMPLE_DOUBLE_FB

#define EXAMPLE_LVGL_TICK_PERIOD_MS    2



#define BUFCOLUMNS 532 // EXAMPLE_LCD_H_RES
#if SCALE85
#define BUFLINES 300
#else
#define BUF1LINES EXAMPLE_LCD_V_RES
#endif

// we use two semaphores to sync the VSYNC event and the LVGL task, to avoid potential tearing effect
SemaphoreHandle_t sem_vsync_end;
SemaphoreHandle_t sem_gui_ready;

//SemaphoreHandle_t sem_emu_request;     // scaler to emulator: request next 5 lines
QueueHandle_t scaler_to_emu;

volatile extern int v06x_framecount;
volatile int framecount = 0;
volatile int bounce_empty_ctr = 0;
volatile int fps = 0;
volatile int v06x_fps = 0;
volatile int bounce_core = 0;
volatile int filling = 0;
volatile int v06x_frame_cycles = 0;

volatile uint64_t lastframe_us = 0;
volatile uint64_t frameduration_us = 0;


uint32_t read_buffer_index;
uint8_t * bounce_buf8[2];

// this is pointless because everything is global here
typedef struct user_context_ {
} user_context_t;

user_context_t user_context;

static bool example_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    frameduration_us = esp_timer_get_time() - lastframe_us;
    lastframe_us = esp_timer_get_time();

    //esp_lcd_rgb_panel_restart(panel);
    BaseType_t high_task_awoken = pdFALSE;
    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE) {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }
    ++framecount;
    return high_task_awoken == pdTRUE;
}

IRAM_ATTR static uint16_t inline
bgr233torgb565(uint8_t c)
{
    uint16_t r = (c & 7) << 2;
    uint16_t g = ((c >> 3) & 7) << 3;
    uint16_t b = ((c >> 6) & 3) << 3;
    return (r << 11) | (g << 5) | b;
}

#define C8TO16(c)   (((((c) & 7) * 4) << 11) | (((((c) >> 3) & 7) * 8) << 5) | ((((c) >> 6) & 3) * 8))


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
        ofs += EXAMPLE_LCD_H_RES;  // + 0
    c16_1 = C8TO16(s4_1 >> 0);
    c16_2 = C8TO16(s4_1 >> 8);
    c16_3 = C8TO16(s4_1 >> 16);
    c16_4 = C8TO16(s4_1 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // + 0.625
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // + 1.25
    c16_1 = C8TO16(s4_2 >> 0);
    c16_2 = C8TO16(s4_2 >> 8);
    c16_3 = C8TO16(s4_2 >> 16);
    c16_4 = C8TO16(s4_2 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // + 1.875
    c16_1 = C8TO16(s4_3 >> 0);
    c16_2 = C8TO16(s4_3 >> 8);
    c16_3 = C8TO16(s4_3 >> 16);
    c16_4 = C8TO16(s4_3 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // + 2.5
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // + 3.125
    c16_1 = C8TO16(s4_4 >> 0);
    c16_2 = C8TO16(s4_4 >> 8);
    c16_3 = C8TO16(s4_4 >> 16);
    c16_4 = C8TO16(s4_4 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // + 3.75
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // + 4.375
}

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
        ofs += EXAMPLE_LCD_H_RES;  // dst_line = 1
    // y + 0.6, y + 1.2
    c16_1 = C8TO16(s4_1 >> 0);
    c16_2 = C8TO16(s4_1 >> 8);
    c16_3 = C8TO16(s4_1 >> 16);
    c16_4 = C8TO16(s4_1 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;   // dst_line = 2
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;   // dst_line = 3
    // y + 1.8, y + 2.4
    c16_1 = C8TO16(s4_2 >> 0);
    c16_2 = C8TO16(s4_2 >> 8);
    c16_3 = C8TO16(s4_2 >> 16);
    c16_4 = C8TO16(s4_2 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // dst_line = 4
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // dst_line = 5
    // y = 3.0
    c16_1 = C8TO16(s4_3 >> 0);
    c16_2 = C8TO16(s4_3 >> 8);
    c16_3 = C8TO16(s4_3 >> 16);
    c16_4 = C8TO16(s4_3 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // dst_line = 6
    // y + 3.6, y + 4.2
    c16_1 = C8TO16(s4_4 >> 0);
    c16_2 = C8TO16(s4_4 >> 8);
    c16_3 = C8TO16(s4_4 >> 16);
    c16_4 = C8TO16(s4_4 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // dst_line = 7
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // dst_line = 8
    // y + 4.8, y + 5.4
    c16_1 = C8TO16(s4_5 >> 0);
    c16_2 = C8TO16(s4_5 >> 8);
    c16_3 = C8TO16(s4_5 >> 16);
    c16_4 = C8TO16(s4_5 >> 24);
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        ofs += EXAMPLE_LCD_H_RES;  // dst_line = 9
        col[ofs] = c16_1; col[ofs+1] = c16_2; col[ofs+2] = c16_2; col[ofs+3] = c16_3; col[ofs+4] = c16_4;
        //ofs += EXAMPLE_LCD_H_RES;  // dst_line = 10
}

constexpr int NTUTU = 150;
static int tutu_i = -1;
static uint64_t tutu[NTUTU];
static int tutu_pos_px[NTUTU];
static int tutu_len_bytes[NTUTU];
static int tutu_frm = 0;
static int tutu_frm_reg = 0;

static bool IRAM_ATTR 
on_bounce_empty_event(esp_lcd_panel_handle_t panel, void *bounce_buf, int pos_px, int len_bytes, void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xQueueSendFromISR(scaler_to_emu, &pos_px, &high_task_awoken);

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

    uint8_t * buf8 = bounce_buf8[read_buffer_index];
    uint16_t * bounce16 = (uint16_t *)bounce_buf;

    int border_w = (EXAMPLE_LCD_H_RES - BUFCOLUMNS * 5/4) / 2;

    uint8_t * bbuf = static_cast<uint8_t *>(bounce_buf);

    for (int i = 0; i < BOUNCE_NLINES; ++i) {
        memset(bbuf, 0xff && read_buffer_index, border_w << 1);
        if (pos_px == 0) memset(bbuf, 0xffff, 16);
        memset(bbuf + 2 * BUFCOLUMNS * 5/4 + (border_w << 1), 0, (border_w << 1) + 2); // extra pixel because border_w is odd
        bbuf += EXAMPLE_LCD_H_RES * 2;
    }

    for (int dst_x = border_w, src_x = 0; dst_x + 5 < EXAMPLE_LCD_H_RES - border_w; dst_x += 5, src_x += 4) {
        fillcolumn_h54_v106(bounce16 + dst_x, reinterpret_cast<uint32_t *>(buf8 + src_x));
    }

    // flip buffers for the next time
    read_buffer_index ^= 1;

    ++bounce_empty_ctr;
    bounce_core = esp_cpu_get_core_id();
    return true;
}

static void tick_1s(void *arg)
{
    fps = framecount;
    framecount = 0;  

    v06x_fps = v06x_framecount;
    v06x_framecount = 0;
}

volatile uint32_t frame_no = 0;

void IRAM_ATTR fill_buf(uint16_t * buf)
{
    filling = 1;

    uint16_t * bufptr = buf;
    for (size_t yy = 0; yy < BUFLINES; yy++)
    {
        size_t y = (yy + frame_no) % BUFLINES;
        for (size_t xx = 0; xx < EXAMPLE_LCD_H_RES; xx+=8)
        {
            size_t x = (xx + frame_no * 8) % EXAMPLE_LCD_H_RES;
            uint32_t r = 31 * x / (EXAMPLE_LCD_H_RES - 1);
            uint32_t g = 31 * y / (BUFLINES - 1);
            uint32_t b = 0x3f & (r ^ g);

            uint16_t color = (r << 11) | (g << 5) | b;
            //buf[yy * EXAMPLE_LCD_H_RES + xx] = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
        }

        buf[yy * EXAMPLE_LCD_H_RES] = 0xffff;
    }   

    filling = 0;     
}

void IRAM_ATTR fill_buf8(uint8_t * buf)
{
    filling = 1;

    uint8_t * bufptr = buf;
    for (size_t yy = 0; yy < BUFLINES; yy++)
    {
        size_t y = (yy + frame_no) % BUFLINES;
        for (size_t xx = 0; xx < BUFCOLUMNS; xx+=4)
        {
            size_t x = (xx + frame_no * 8) % BUFCOLUMNS;
            uint32_t r = 7 * x / (BUFCOLUMNS - 1);
            uint32_t g = 7 * y / (BUFLINES - 1);
            uint32_t b = 0x3 & (r ^ g);

            uint8_t color = (b << 6) | (g << 3) | r;
            //buf[yy * EXAMPLE_LCD_H_RES + xx] = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
            *bufptr++ = color;
        }

        buf[yy * BUFCOLUMNS] = 0xff;
        buf[yy * BUFCOLUMNS + yy] = 0xff;
        buf[yy * BUFCOLUMNS + (BUFLINES - yy)] = 0xff;
    }   

    filling = 0;     
}

void create_lcd_driver_task(void *pvParameter)
{
    xSemaphoreTake(sem_gui_ready, portMAX_DELAY);

    ESP_LOGI(TAG, "Install RGB LCD panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ,
            .h_res = EXAMPLE_LCD_H_RES,
            .v_res = EXAMPLE_LCD_V_RES,
            // The following parameters should refer to LCD spec
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            // .vsync_pulse_width = 32,
            // .vsync_back_porch = 16,
            // .vsync_front_porch = 16,
            .flags = {
                .hsync_idle_low = 1,
                .vsync_idle_low = 1,
                .de_idle_high = 0,
                .pclk_active_neg = true,
                .pclk_idle_high = 0,
            }
        },
        .data_width = 16, // RGB565 in parallel mode, thus 16bit in width
        .num_fbs = 0, //EXAMPLE_LCD_NUM_FB,
#if CONFIG_EXAMPLE_USE_BOUNCE_BUFFER
        .bounce_buffer_size_px = BOUNCE_NLINES * EXAMPLE_LCD_H_RES,
#endif
        .sram_trans_align = 8,
        .psram_trans_align = 64,
        .hsync_gpio_num = EXAMPLE_PIN_NUM_HSYNC,
        .vsync_gpio_num = EXAMPLE_PIN_NUM_VSYNC,
        .de_gpio_num = EXAMPLE_PIN_NUM_DE,
        .pclk_gpio_num = EXAMPLE_PIN_NUM_PCLK,
        .disp_gpio_num = EXAMPLE_PIN_NUM_DISP_EN,
        .data_gpio_nums = {
            EXAMPLE_PIN_NUM_DATA0,
            EXAMPLE_PIN_NUM_DATA1,
            EXAMPLE_PIN_NUM_DATA2,
            EXAMPLE_PIN_NUM_DATA3,
            EXAMPLE_PIN_NUM_DATA4,
            EXAMPLE_PIN_NUM_DATA5,
            EXAMPLE_PIN_NUM_DATA6,
            EXAMPLE_PIN_NUM_DATA7,
            EXAMPLE_PIN_NUM_DATA8,
            EXAMPLE_PIN_NUM_DATA9,
            EXAMPLE_PIN_NUM_DATA10,
            EXAMPLE_PIN_NUM_DATA11,
            EXAMPLE_PIN_NUM_DATA12,
            EXAMPLE_PIN_NUM_DATA13,
            EXAMPLE_PIN_NUM_DATA14,
            EXAMPLE_PIN_NUM_DATA15,
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

extern "C"
void app_main(void)
{
    ESP_LOGI(TAG, "Create semaphores");
    sem_vsync_end = xSemaphoreCreateBinary();
    assert(sem_vsync_end);
    sem_gui_ready = xSemaphoreCreateBinary();
    assert(sem_gui_ready);
    //sem_emu_request = xSemaphoreCreateBinary();
    //assert(sem_emu_request);
    scaler_to_emu = xQueueCreate(2, sizeof(int));

    gpio_config_t bk_gpio_config = {
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
    gpio_set_level(static_cast<gpio_num_t>(EXAMPLE_PIN_NUM_BK_LIGHT), EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

#ifdef BOUNCE_BUFFERS_APART
    // make a large gap between bounce buffers,  hoping that they are in different banks
    uint8_t * evilbuf = static_cast<uint8_t *>(heap_caps_malloc((BUFCOLUMNS * 5 * sizeof(uint8_t) + 32) * 2 + 32768, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT)); // alignment offset for main screen area
    bounce_buf8[0] = &evilbuf[6];
    bounce_buf8[1] = &evilbuf[32768 + 6];
#else
    bounce_buf8[0] = static_cast<uint8_t *>(heap_caps_malloc(BUFCOLUMNS * 6 * sizeof(uint8_t) + 32, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT)) + 6; // alignment offset for main screen area
    assert(bounce_buf8[0]);
    bounce_buf8[1] = static_cast<uint8_t *>(heap_caps_malloc(BUFCOLUMNS * 6 * sizeof(uint8_t) + 32, MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT)) + 6; // alignment offset for main screen area
    assert(bounce_buf8[1]);
#endif
    memset(bounce_buf8[0], 0, BUFCOLUMNS * 6 * sizeof(uint8_t));
    memset(bounce_buf8[1], 0, BUFCOLUMNS * 6 * sizeof(uint8_t));
    ESP_LOGI(TAG, "Created frame buffers: buf8[0]=%p buf8[1]=%p", bounce_buf8[0], bounce_buf8[1]);

    // init scaler task on core 1 to pin scaler to core 1
    xTaskCreatePinnedToCore(&create_lcd_driver_task, "lcd_init_task", 1024*4, NULL, configMAX_PRIORITIES - 1, NULL, SCALER_CORE);
    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);

    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(static_cast<gpio_num_t>(EXAMPLE_PIN_NUM_BK_LIGHT), EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    //fill_buf8(user_context.buf8);
    vTaskDelay(pdMS_TO_TICKS(500));

    v06x_init(scaler_to_emu, bounce_buf8[0], bounce_buf8[1]);
    xTaskCreatePinnedToCore(&v06x_task, "v06x", 1024*4, NULL, 2 /*configMAX_PRIORITIES - 2*/, NULL, EMU_CORE);

    int last_frame_cycles = 0;
    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        //vTaskDelay(pdMS_TO_TICKS(1000));
        xSemaphoreGive(sem_gui_ready);
        xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
        ++frame_no;

        // if (index_d >= NSAMP) {
        //     for (int i = 0; i < NSAMP; ++i) {
        //         printf("%d pos_px=%d len_bytes=%d\n", i, pos_px_d[i], len_bytes_d[i]);
        //     }
        // }
        
        if (frame_no % 50 == 0) {
            printf("fps=%d v06x_fps=%d cycles=%d cb/frm=%d frame dur=%lluus\n", fps, v06x_fps, (v06x_frame_cycles - last_frame_cycles) / 50, tutu_frm_reg, frameduration_us);
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
