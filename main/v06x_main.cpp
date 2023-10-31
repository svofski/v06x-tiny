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
#include "ay.h"
#include "wav.h"
#include "util.h"
#include "version.h"
#include "dummy_debug.h"
#include "options.h"
#include "esp_filler.h"

#include "testroms.h"

extern const char *TAG;

volatile int v06x_framecount = 0;

static Memory * memory;
static QueueHandle_t que_scaler_to_emu;
static uint8_t * buf0;
static uint8_t * buf1;

void v06x_init(QueueHandle_t from_scaler, uint8_t * _buf0, uint8_t * _buf1)
{
    que_scaler_to_emu = from_scaler;
    buf0 = _buf0;
    buf1 = _buf1;
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

void benchmark_vi53(I8253 * vi53)
{
    const unsigned MEASUREMENTS = 312 * 50 * 30; // 30 seconds

    uint64_t start = esp_timer_get_time();

    vi53->write(3, 0x36); // 00 11 011 0
    vi53->write(0, (130)&255);
    vi53->write(0, (130)>>8);

    // vi53->write(0, (96*4)&255);
    // vi53->write(0, (96*4)>>8);

    // vi53->write(3, 0x76); 
    // vi53->write(1, (96*5)&255); 
    // vi53->write(1, (96*5)>>8); 

    // vi53->write(3, 0xb6); 
    // vi53->write(2, (96*6)&255); 
    // vi53->write(2, (96*6)>>8); 

    for (int retries = 0; retries < MEASUREMENTS; retries++) {
        vi53->count_clocks(96);
    }
    uint64_t end = esp_timer_get_time();
    printf("TIMER (mode 3) %u clocks took %llu microseconds\n", MEASUREMENTS, end - start);
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
    AY* ay = new AY();
    AYWrapper* aw = new AYWrapper(*ay);

    IO* io = new IO(*memory, *keyboard, *timer, *fdc, *ay, *tape_player, esp_filler::palette8());
    TV* tv = new TV();
    
    // keep it as a dummy
    PixelFiller* filler = new PixelFiller(*memory, *io, *tv);
    ESP_LOGI(TAG, "PixelFiller: %p sizeof()=%u", filler, sizeof(PixelFiller));

    esp_filler::init(reinterpret_cast<uint32_t *>(memory->buffer()), io, buf0, buf1, timer);
    esp_filler::frame_start();

    //filler = reinterpret_cast<PixelFiller *>(heap_caps_realloc(filler, sizeof(PixelFiller), MALLOC_CAP_SPIRAM));
    //ESP_LOGI(TAG, "PixelFiller: moved to internal DRAM: %p", filler);

    Board* board = new Board(*memory, *io, *filler, *tv, *tape_player, *debug);

    ESP_LOGI(TAG, "Board: %p", board);

#if 1

    filler->init();
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

    // benchmark_bob(board);
    benchmark_vi53(timer);
    //benchmark(board);
    //test_loop(board);

#define SHOP_MODE 1
#if SHOP_MODE
    esp_filler::bob(50);

    struct romset_t {
        const uint8_t * rom;
        size_t len;
        int nframes;
    };

    romset_t romset[] = {
        //{&ROM(bolderm)[0], ROMLEN(bolderm), 60 * 50},
        {&ROM(cronex)[0], ROMLEN(cronex), 60 * 50},
        // -- weird sound, no picture {&ROM(cybermut)[0], ROMLEN(cybermut), 60 * 50},
        // -- some noises, bolderm has something similar as well -- trtrtrtr in the next program
        //{&ROM(wave)[0], ROMLEN(wave), 75 * 50},
        //{&ROM(progdemo)[0], ROMLEN(progdemo), 120 * 50},
        ..{&ROM(spsmerti)[0], ROMLEN(spsmerti), 45 * 50},
        {&ROM(mclrs)[0], ROMLEN(mclrs), 4 * 50},
        {&ROM(tiedye2)[0], ROMLEN(tiedye2), 4 * 50},
        {&ROM(kittham1)[0], ROMLEN(kittham1), 4 * 50},
        {&ROM(clrspace)[0], ROMLEN(clrspace), 4 * 50},
        {&ROM(oblitterated)[0], ROMLEN(oblitterated), 2 * 60 * 50},
        {&ROM(arzak)[0], ROMLEN(arzak), 2 * 60 * 50},
        {&ROM(s8snail)[0], ROMLEN(s8snail), 50 * 50},
        {&ROM(bord)[0], ROMLEN(bord), 10 * 50},
        {&ROM(bord2)[0], ROMLEN(bord2), 10 * 50},
        {&ROM(bazis)[0], ROMLEN(bazis), 60 * 50},
        {&ROM(sunsetb)[0], ROMLEN(sunsetb), 30 * 50},
        {&ROM(hscroll)[0], ROMLEN(hscroll), 10 * 50},
    };

    for (int ri = 0;;) {
        board->reset(Board::ResetMode::BLKVVOD);
        //timer->reset();
        esp_filler::bob(50);
        for (size_t i = 0; i < romset[ri].len; ++i) {
            memory->write(256 + i, romset[ri].rom[i], false);
        }
        board->reset(Board::ResetMode::BLKSBR);
        printf("loaded rom %d, running %d frames ", ri, romset[ri].nframes);
        esp_filler::bob(romset[ri].nframes);

        if (++ri == sizeof(romset)/sizeof(romset[0])) ri = 0;
    }
#endif
    // for (size_t i = 0; i < tiedye2_rom_len; ++i) {
    //     memory->write(256 + i, tiedye2_rom[i], false);
    // }
    // for (size_t i = 0; i < kittham1_rom_len; ++i) {
    //     memory->write(256 + i, kittham1_rom[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(clrspace); ++i) {
    //     memory->write(256 + i, ROM(clrspace)[i], false);
    // }
    // for (size_t i = 0; i < oblitterated_rom_len; ++i) {
    //     memory->write(256 + i, oblitterated_rom[i], false);
    // }
    // for (size_t i = 0; i < arzak_rom_len; ++i) {
    //     memory->write(256 + i, arzak_rom[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(s8snail); ++i) {
    //     memory->write(256 + i, ROM(s8snail)[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(bord); ++i) {
    //     memory->write(256 + i, ROM(bord)[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(bord2); ++i) {
    //     memory->write(256 + i, ROM(bord2)[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(bazis); ++i) {
    //     memory->write(256 + i, ROM(bazis)[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(wave); ++i) {
    //     memory->write(256 + i, ROM(wave)[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(sunsetb); ++i) {
    //     memory->write(256 + i, ROM(sunsetb)[i], false);
    // }
    // for (size_t i = 0; i < ROMLEN(hscroll); ++i) {
    //     memory->write(256 + i, ROM(hscroll)[i], false);
    // }

    // printf("loaded oblitterated\n");
    // board->reset(Board::ResetMode::BLKSBR);

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


