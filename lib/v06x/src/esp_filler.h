#include <cstdint>
#include "vio.h"
#include "8253.h"
#include "globaldefs.h"
#include <functional>

namespace esp_filler {
    constexpr int center_offset = DEFAULT_CENTER_OFFSET;
    constexpr int screen_width = DEFAULT_SCREEN_WIDTH;
    constexpr int first_visible_line = 24; // typical v06x: 24
    constexpr int first_raster_line = 40;
    constexpr int last_raster_line = 40 + 256;
    constexpr int last_visible_line = 311;

    extern int raster_line;
    extern int irq;
    extern int inte;
    extern int write_buffer;
    extern int ay_bufpos_reg;

    extern volatile int v06x_framecount;
    extern volatile int v06x_frame_cycles;

    extern std::function<void(ResetMode)> onreset;
    extern std::function<void(void)> onosd;

    //uint16_t * palette8();    
    void init(uint32_t * _mem32, IO * _io, uint8_t * buf1, uint8_t * buf2, I8253 * vi53);
    void frame_start();
    int fill(int ncycles, int commit_time, int commit_time_pal);
    int fill_noout(int ncycles);
    int fill_void(int ncycles, int commit_time, int commit_time_pal);
    int fill_void_noout(int ncycles);
    int fake_fill(int ncycles, int commit_time, int commit_time_pal);
    void write_pal(uint8_t adr8, uint8_t rgb);

    int bob(int maxframes);
}