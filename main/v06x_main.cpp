#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "params.h"
#include "sync.h"
#include "memory.h"
#include "fd1793.h"
#include "vio.h"
#include "board.h"
#include "options.h"
#include "keyboard.h"
#include "8253.h"
#include "ay.h"
#include "wav.h"
#include "util.h"
#include "version.h"
#include "options.h"
#include "esp_filler.h"
#include "sdcard.h"

#include "testroms.h"

extern SDCard sdcard;

namespace v06x 
{

IO * io;
Wav * wav;

static Memory * memory;
static QueueHandle_t que_scaler_to_emu;
static uint8_t * buf0;
static uint8_t * buf1;

static void v06x_task(void *param);

void load_blob(void);
void rom_blob_loaded(Board * board);
void fdd_loaded(int disk, FD1793 * fdc);

void init(QueueHandle_t from_scaler, uint8_t * _buf0, uint8_t * _buf1)
{
    que_scaler_to_emu = from_scaler;
    buf0 = _buf0;
    buf1 = _buf1;
}

void create_pinned_to_core()
{
    xTaskCreatePinnedToCore(&v06x_task, "v06x", 1024*6, NULL, EMU_PRIORITY, NULL, EMU_CORE);
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
    Options.log.fdc = false;

    memory = new Memory(); // hopefully it gets allocated in PSRAM
    assert(memory);
    ESP_LOGI(TAG, "Alocated v06x memory: %p", memory);
    //memory = static_cast<Memory *>(heap_caps_realloc(memory, sizeof(Memory), MALLOC_CAP_INTERNAL));
    //ESP_LOGI(TAG, "Memory: moved to internal DRAM: %p", memory);

    FD1793 * fdc = new FD1793();
    ESP_LOGI(TAG, "FDC: %p", fdc);

    wav = new Wav();
    WavPlayer * tape_player = new WavPlayer(*wav);
    I8253* timer = new I8253();
    
    //// SPI keyboard
    //keyboard::init();


#if 0
    for(;;) {
        uint8_t rows[8];
        int shifter = 1;
        for (int i = 0; i < 8; ++i, shifter <<= 1) {
            keyboard::select_columns(shifter ^ 0xff);
            keyboard::read_rows();
            rows[i] = keyboard::state.rows;
        }
        keyboard::read_modkeys();
        for (int i = 0; i < 8; ++i) {
            printf("%02x ", rows[i]);
        }
        printf(" mods=%02x\n", keyboard::state.pc);
        vTaskDelay(100/portTICK_PERIOD_MS);
    }
#endif

    // AY Sound
    AySound::init();
    AySound::set_sound_format(AUDIO_SAMPLERATE, 1, 8);
    AySound::set_stereo(AYEMU_MONO, NULL);
    AySound::reset();

    io = new IO(*memory, *timer, *fdc, *tape_player);
    Board* board = new Board(*memory, *io, *tape_player);
    ESP_LOGI(TAG, "Board: %p", board);
    assert(board);

    esp_filler::init(reinterpret_cast<uint32_t *>(memory->buffer()), io, buf0, buf1, timer, tape_player);
    esp_filler::onreset = [board](ResetMode blkvvod) {
        board->reset(blkvvod);
    };
    esp_filler::frame_start();    

    board->init();
    fdc->init();
    if (Options.autostart) {
        int seq = 0;
        io->onruslat = [&seq,board](bool ruslat) {
            seq = (seq << 1) | (ruslat ? 1 : 0);
            if ((seq & 15) == 6) {
                board->reset(ResetMode::BLKSBR);
                io->onruslat = nullptr;
            }
        };
    }
    ESP_LOGI(TAG, "Board: %p", board);

    board->reset(ResetMode::BLKVVOD);

    // benchmark_bob(board);
    //benchmark_vi53(timer);
    //benchmark(board);
    //test_loop(board);

#define SHOP_MODE 0
#if SHOP_MODE
    esp_filler::bob(50);

    struct romset_t {
        const uint8_t * rom;
        size_t len;
        int nframes;
    };

    romset_t romset[] = {
        //{&ROM(GameNoname)[0], ROMLEN(GameNoname), 600 * 50 * 0}, // jdundel aeterna
        //{&ROM(kdadrtst)[0], ROMLEN(kdadrtst), 60 * 50},
        //{&ROM(kdtest)[0], ROMLEN(kdtest), 60 * 50},
        //{&ROM(testtp)[0], ROMLEN(testtp), 60 * 50},
        //{&ROM(bolderm)[0], ROMLEN(bolderm), 60 * 50},
        //{&ROM(incurzion)[0], ROMLEN(incurzion), 0},
        {&ROM(bas299)[0], ROMLEN(bas299), 0 * 60 * 50},
        //{&ROM(baskor)[0], ROMLEN(baskor), 0 * 15 * 50},
        {&ROM(bazis)[0], ROMLEN(bazis), 60 * 50},
        {&ROM(text80)[0], ROMLEN(text80), 60 * 50},
        {&ROM(mineswep)[0], ROMLEN(mineswep), 30 * 50},
        {&ROM(cybermut)[0], ROMLEN(cybermut), 60 * 50},
        {&ROM(hwdit512)[0], ROMLEN(hwdit512), 15 * 50},
        //{&ROM(dizrek_)[0], ROMLEN(dizrek_), 60 * 50}, // needs at least 600s to finish, slowdown and flickering near the end
        {&ROM(hscroll)[0], ROMLEN(hscroll), 10 * 50},
        {&ROM(hiblue7c)[0], ROMLEN(hiblue7c), 15 * 50},
        {&ROM(clrspace)[0], ROMLEN(clrspace), 4 * 50},
        {&ROM(ses)[0], ROMLEN(ses), 120 * 50},
        {&ROM(oblitterated)[0], ROMLEN(oblitterated), 110 * 50},
        {&ROM(arzak)[0], ROMLEN(arzak), 2 * 60 * 50},
        {&ROM(cronex)[0], ROMLEN(cronex), 60 * 50},
        {&ROM(progdemo)[0], ROMLEN(progdemo), 130 * 50},
        {&ROM(mclrs)[0], ROMLEN(mclrs), 4 * 50},
        {&ROM(tiedye2)[0], ROMLEN(tiedye2), 4 * 50},
        {&ROM(spsmerti)[0], ROMLEN(spsmerti), 45 * 50},
        {&ROM(kittham1)[0], ROMLEN(kittham1), 4 * 50},
        {&ROM(eightsnail)[0], ROMLEN(eightsnail), 45 * 50},        
        {&ROM(bord)[0], ROMLEN(bord), 10 * 50},
        {&ROM(bord2)[0], ROMLEN(bord2), 10 * 50},
        {&ROM(sunsetb)[0], ROMLEN(sunsetb), 15 * 50},
        {&ROM(wave)[0], ROMLEN(wave), 75 * 50},
    };

    for (int ri = 0;;) {
        board->reset(ResetMode::BLKVVOD);
        AySound::reset();
        timer->reset();
        esp_filler::bob(50);
        for (size_t i = 0; i < romset[ri].len; ++i) {
            memory->write(256 + i, romset[ri].rom[i], false);
        }
        board->reset(ResetMode::BLKSBR);
        printf("loaded rom %d, running %d frames ", ri, romset[ri].nframes);
        esp_filler::bob(romset[ri].nframes);

        if (++ri == sizeof(romset)/sizeof(romset[0])) ri = 0;
    }
#endif

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

    while (1) {
        // spin until break
        esp_filler::bob(0);

        // break / asset loaded
        switch (sdcard.blob.kind) {
            case AK_ROM:
                AySound::reset();
                timer->reset();
                rom_blob_loaded(board);
                break;
            case AK_FDD:
                fdd_loaded(0, fdc);
                break;
            case AK_WAV:
                // there's nothing special to do
                break;
            default:
                break;
        }
    }
}

std::unique_ptr<DiskImage> blob_dsk;

// called from low priority task on sdcard.osd_notify_queue
void blob_loaded()
{
    auto &bytes = reinterpret_cast<std::vector<uint8_t> &>(sdcard.blob.bytes); // erase allocator

    switch (sdcard.blob.kind) {
        case AK_FDD:
            printf("blob_loaded: fdd (%d bytes)\n", bytes.size());
            blob_dsk = make_unique<DetachedDiskImage>(bytes);
            break;
        case AK_WAV:
            // this will also rewind the player
            printf("blob_loaded: wav (%d bytes)\n", bytes.size());
            wav->set_bytes(bytes);
            break;
        case AK_ROM:
            printf("blob_loaded: rom (%d bytes)\n", bytes.size());
            break;
        case AK_BAS:
            printf("blob_loaded: bas (%d bytes)\n", bytes.size());
            break;
        default:
            printf("blob_loaded: unknown %d (%d bytes)\n", sdcard.blob.kind, bytes.size());
            break;
    }

    int cmd = CMD_EMU_BREAK;
    xQueueSend(::emu_command_queue, &cmd, portMAX_DELAY);
}

// called from main emulator loop, should be as fast as possible
void rom_blob_loaded(Board * board)
{
    board->reset(ResetMode::BLKVVOD);
    esp_filler::bob(50);
    for (size_t i = 0; i < sdcard.blob.bytes.size(); ++i) {
        memory->write(256 + i, sdcard.blob.bytes[i], false);
    }
    board->reset(ResetMode::BLKSBR);
    printf("loaded rom %d\n", sdcard.blob.bytes.size());
}

// called from main emulator loop, should be as fast as possible
void fdd_loaded(int disk, FD1793 * fdc)
{
    fdc->disk(disk).attach(std::move(blob_dsk));
    printf("attached (%d) %d bytes\n", disk, sdcard.blob.bytes.size());

    #if 0
    printf("after attachment blob_dsk=%p\n", blob_dsk.get());
    for (int i = 0; i < 256; ++i) {
        printf("%02x ", fdc->disk(disk).dsk->get(i));
    }
    printf("\n");
    #endif
}

void wav_loaded()
{
}

}
