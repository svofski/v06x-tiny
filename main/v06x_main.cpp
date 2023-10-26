#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"


#include "memory.h"
#include "fd1793.h"
#include "vio.h"
#include "tv.h"
#include "board.h"
#include "options.h"
#include "keyboard.h"
#include "8253.h"
#include "sound.h"
#include "ay.h"
#include "wav.h"
#include "util.h"
#include "version.h"
#include "dummy_debug.h"
#include "options.h"
#include "esp_filler.h"

extern const char *TAG;

volatile int v06x_framecount = 0;

static Memory * memory;
static QueueHandle_t que_scaler_to_emu;
static uint8_t * buf0;
static uint8_t * buf1;

extern "C" unsigned char wave_rom[];
extern "C" unsigned int wave_rom_len;

extern "C" unsigned char oblitterated_rom[];
extern "C" unsigned int oblitterated_rom_len;

extern "C" unsigned char arzak_rom[];
extern "C" unsigned int arzak_rom_len;

extern "C" unsigned char v06x_rom[];
extern "C" unsigned int v06x_rom_len;

extern "C" unsigned char s8snail_rom[];
extern "C" unsigned int s8snail_rom_len;

extern "C" unsigned char clrs_rom[];
extern "C" unsigned int clrs_rom_len;

extern "C" unsigned char clrspace_rom[];
extern "C" unsigned int clrspace_rom_len;

extern "C" unsigned char kittham1_rom[];
extern "C" unsigned int kittham1_rom_len;

extern "C" unsigned char tiedye2_rom[];
extern "C" unsigned int tiedye2_rom_len;

void v06x_init(QueueHandle_t from_scaler, uint8_t * _buf0, uint8_t * _buf1)
{
    que_scaler_to_emu = from_scaler;
    buf0 = _buf0;
    buf1 = _buf1;
}

void benchmark(Board * board)
{
    const unsigned MEASUREMENTS = 50;
    uint64_t start = esp_timer_get_time();

    for (int retries = 0; retries < MEASUREMENTS; retries++) {
        board->esp_freewheel_until_top();

        board->esp_execute_five();
        for (int i = 2; i < 60; ++i) {
            board->esp_execute_five();
        }

    }

    uint64_t end = esp_timer_get_time();

    printf("%u iterations took %llu milliseconds (%llu microseconds per invocation)\n",
           MEASUREMENTS, (end - start)/1000, (end - start)/MEASUREMENTS);
}    

void benchmark_bob(Board * board)
{
    const unsigned MEASUREMENTS = 50;
    uint64_t start = esp_timer_get_time();

    esp_filler::bob(MEASUREMENTS);

    uint64_t end = esp_timer_get_time();

    printf("%u iterations took %llu milliseconds (%llu microseconds per invocation)\n",
           MEASUREMENTS, (end - start)/1000, (end - start)/MEASUREMENTS);
}    


void test_loop(Board * board)
{
    for (int retries = 0; retries < 2; retries++) {
        printf("0 raster_line=%d buffer=%d\n", esp_filler::raster_line, esp_filler::write_buffer);
        board->esp_freewheel_until_top();
        printf("1 raster_line=%d buffer=%d\n", esp_filler::raster_line, esp_filler::write_buffer);

        board->esp_execute_five();
        for (int i = 2; i < 60; ++i) {
            board->esp_execute_five();
            printf("2 raster_line=%d buffer=%d\n", esp_filler::raster_line, esp_filler::write_buffer);
        }

        printf("3 raster_line=%d buffer=%d\n", esp_filler::raster_line, esp_filler::write_buffer);
    }
}    


void v06x_task(void *param)
{
    Options.nosound = true;
    Options.nofilter = true;

    memory = new Memory(); // hopefully it gets allocated in PSRAM
    assert(memory);
    ESP_LOGI(TAG, "Alocated v06x memory: %p", memory);
    memory = static_cast<Memory *>(heap_caps_realloc(memory, sizeof(Memory), MALLOC_CAP_INTERNAL));
    ESP_LOGI(TAG, "Memory: moved to internal DRAM: %p", memory);


    Debug * debug = new Debug(memory);
    ESP_LOGI(TAG, "Debug: %p", debug);
    assert(debug);

    FD1793 * fdc = new FD1793();
    ESP_LOGI(TAG, "FDC: %p", fdc);

    Wav* wav = new Wav();
    WavPlayer* tape_player = new WavPlayer(*wav);
    Keyboard* keyboard = new Keyboard();
    I8253* timer = new I8253();
    TimerWrapper* tw = new TimerWrapper(*timer);
    AY* ay = new AY();
    AYWrapper* aw = new AYWrapper(*ay);

    Soundnik* soundnik = new Soundnik(*tw, *aw);
    IO* io = new IO(*memory, *keyboard, *timer, *fdc, *ay, *tape_player, esp_filler::palette8());
    TV* tv = new TV();
    
    // keep it as a dummy
    PixelFiller* filler = new PixelFiller(*memory, *io, *tv);
    ESP_LOGI(TAG, "PixelFiller: %p sizeof()=%u", filler, sizeof(PixelFiller));

    esp_filler::init(reinterpret_cast<uint32_t *>(memory->buffer()), io, buf0, buf1);
    esp_filler::frame_start();

    //filler = reinterpret_cast<PixelFiller *>(heap_caps_realloc(filler, sizeof(PixelFiller), MALLOC_CAP_SPIRAM));
    //ESP_LOGI(TAG, "PixelFiller: moved to internal DRAM: %p", filler);

    Board* board = new Board(*memory, *io, *filler, *soundnik, *tv, *tape_player, *debug);

    ESP_LOGI(TAG, "Board: %p", board);

#if 1

    filler->init();
    soundnik->init(0);    // this may switch the audio output off
    tv->init();
    board->init();
    fdc->init();
    if (Options.bootpalette) {
        io->yellowblue();
    }

    keyboard->onreset = [board](bool blkvvod) {
        board->reset(blkvvod ? 
                Board::ResetMode::BLKVVOD : Board::ResetMode::BLKSBR);
    };

    if (Options.autostart) {
        int seq = 0;
        io->onruslat = [&seq,board,io](bool ruslat) {
            seq = (seq << 1) | (ruslat ? 1 : 0);
            if ((seq & 15) == 6) {
                board->reset(Board::ResetMode::BLKSBR);
                io->onruslat = nullptr;
            }
        };
    }
    ESP_LOGI(TAG, "Board: %p", board);

    board->reset(Board::ResetMode::BLKVVOD);

    benchmark_bob(board);
    //benchmark(board);
    //test_loop(board);

    esp_filler::bob(50);

    // for (size_t i = 0; i < tiedye2_rom_len; ++i) {
    //     memory->write(256 + i, tiedye2_rom[i], false);
    // }
    // for (size_t i = 0; i < kittham1_rom_len; ++i) {
    //     memory->write(256 + i, kittham1_rom[i], false);
    // }
    // for (size_t i = 0; i < clrspace_rom_len; ++i) {
    //     memory->write(256 + i, clrspace_rom[i], false);
    // }
    // for (size_t i = 0; i < oblitterated_rom_len; ++i) {
    //     memory->write(256 + i, oblitterated_rom[i], false);
    // }
    for (size_t i = 0; i < arzak_rom_len; ++i) {
        memory->write(256 + i, arzak_rom[i], false);
    }
    // for (size_t i = 0; i < s8snail_rom_len; ++i) {
    //     memory->write(256 + i, s8snail_rom[i], false);
    // }
    printf("loaded oblitterated\n");
    board->reset(Board::ResetMode::BLKSBR);

    //esp_filler::bob(7);
    //i8080cpu::trace_enable = 1;

#ifdef DUMPNIK
    esp_filler::bob(450);
    for(int addr = 0xc000; addr < 0xe000; addr += 16) {
        printf("%04x  ", addr);
        for (int i = 0; i < 16; ++i) {
            printf("%02x ", memory->read(addr + i, false));
        }
        printf("\n");
    }

    for(int fb_row = 0xff; fb_row >= 0; --fb_row) {
        for (int fb_col = 0; fb_col < 32; ++fb_col) {
            size_t addr = ((fb_col & 0xff) << 8) | (fb_row & 0xff);
            uint8_t pxs = memory->read(0xc000 + addr, false);
            for (int i = 0; i < 8; ++i) {
                putchar((pxs & 0x80) ? '*':' ');
                pxs <<= 1;
            }
        }
        printf("\n");
        vTaskDelay(1);
    }
#endif

    esp_filler::bob(0);
    // for (;;) {        
    //     int pos_px = -1;

    //     // freewheel the emulator until we fill lines 0v..4v
    //     board->esp_freewheel_until_top();
    //     //ESP_LOGI(TAG, "v06x_task: freewheel end");
    //     v06x_framecount += 1;
    //     // sync until line 0
    //     while (xQueueReceive(que_scaler_to_emu, &pos_px, portMAX_DELAY)) {
    //         if (pos_px == 0) {
    //             break;
    //         }
    //     }
    //     board->esp_execute_five();
    //     for (int i = 2; i < 60; ++i) {
    //         xQueueReceive(que_scaler_to_emu, &pos_px, portMAX_DELAY);
    //         board->esp_execute_five();
    //     }
    // }
#endif    
}


