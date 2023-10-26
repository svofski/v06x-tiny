#define ESP_DELAYED_COMMIT 1
#define USE_BIT_PERMUTE 1

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_attr.h"
#include <cstdint>

#include "globaldefs.h"
#include "vio.h"
#include "esp_filler.h"
#include "i8080.h"

extern QueueHandle_t scaler_to_emu;
extern int v06x_framecount;
extern int v06x_frame_cycles;

#define TIMED_COMMIT

namespace esp_filler 
{

/*
    v: vector/tv raster line
    u: unscaled lcd line (0..299)
    s: scaled lcd line (0..479)

    2 bounce buffers buf[0]/buf[1]
                            ...
                        0v --------- no framebuffer carefree computing
                        5v  ...
first visible line:     10v switch on buffer writes 
                            select buf[0]
                            compute lines 10v..14v / 0u..4u -> buf[0]
                            wait until 0 comes in on_fill queue
                        15v compute lines 15v..19v / 5u..9u -> buf[1]
                            wait until 8 comes in on_fill queue
                        20v compute lines 20v..24v / 10u..14u -> buf[0]
                            ...
                            wait until 472 comes in on_fill queue
                        300v -- off screen area 
                            switch off buffer writes
                            compute lines 300v..311v
                            ...
*/

constexpr int raster_hstart = center_offset;
constexpr int raster_hend = screen_width + center_offset;


const uint32_t * mem32;
uint32_t pixel32;
uint32_t pixel32_grouped;

union {
    uint8_t * bmp8;          // current write buffer
    uint16_t * bmp16;
    uint32_t * bmp32;
} bmp;

uint8_t * buffers[2];   // bounce buffers 0/1
int write_buffer;

int inte;
int irq;
int irq_clk;
int raster_pixel;   // hard PAL raster pixel (768 per 1/15625s line)
int raster_line;    // hard PAL raster line (312)
int fb_column;
int fb_row;
int vborder;
int visible;
int mode512;
int border_index;
int fiveline_count;
#ifndef TIMED_COMMIT
int color_index;    // index of color at the tip of the beam
#endif

#ifdef TIMED_COMMIT
bool commit_pal;
#endif

IO * io;

// palette ram
uint16_t py2[16];

void init(uint32_t * _mem32, IO * _io, uint8_t * buf1, uint8_t * buf2)
{
    mem32 = &_mem32[0x2000]; // pre-offset 
    io = _io;
    buffers[0] = buf1;
    buffers[1] = buf2;
    write_buffer = 0;
    bmp.bmp8 = buffers[0];
    fiveline_count = 0;        // count groups of 5 lines

    io->onborderchange = [](int border) {
        border_index = border;
    };

    io->onmodechange = [](bool mode) {
        mode512 = mode;
    };

    inte = 0;
}

void frame_start()
{
    // It is tempting to reset the pixel count but the beam is reset in 
    // advanceLine(), don't do that here.
    //this->raster_pixel = 0;   // horizontal pixel counter

    raster_line = 0;    // raster line counter
    fb_column = 0;      // frame buffer column
    fb_row = 0;         // frame buffer row
    vborder = true;     // vertical border flag
    visible = false;    // visible area flag
    irq = false;
}

#if USE_BIT_PERMUTE
IRAM_ATTR
static uint32_t bit_permute_step(uint32_t x, uint32_t m, uint32_t shift) {
    uint32_t t;
    t = ((x >> shift) ^ x) & m;
    x = (x ^ t) ^ (t << shift);
    return x;
}
#endif

IRAM_ATTR
uint32_t fetchNicePixels()
{
    size_t addr = ((fb_column & 0xff) << 8) | (fb_row & 0xff);
    uint32_t x = mem32[addr];
    // 24 16 8 0 25 17 9 1 26 18 10 2 27 19 11 3   28 20 12 4   29 21 13 5  30 22 14 6  31 23 15 7
    x = bit_permute_step(x, 0x00550055, 9);  // Bit index swap+complement 0,3
    x = bit_permute_step(x, 0x00003333, 18);  // Bit index swap+complement 1,4
    x = bit_permute_step(x, 0x000f000f, 12);  // Bit index swap+complement 2,3
    x = bit_permute_step(x, 0x000000ff, 24);  // Bit index swap+complement 3,4
    return x;
}

IRAM_ATTR
inline int shiftNicePixels(uint32_t & nicepixels)
{
    uint32_t modeless = nicepixels >> 28;
    nicepixels <<= 4;
    return modeless;
}


IRAM_ATTR
inline void slab8()
{
    uint32_t nicepixels = fetchNicePixels();
    {
        uint8_t i1 = shiftNicePixels(nicepixels);
        uint8_t i2 = shiftNicePixels(nicepixels);
        uint8_t i3 = shiftNicePixels(nicepixels);
        uint8_t i4 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i1];
        *bmp.bmp16++ = py2[i2];
        *bmp.bmp16++ = py2[i3];
        *bmp.bmp16++ = py2[i4];
    }
    {
        uint8_t i1 = shiftNicePixels(nicepixels);
        uint8_t i2 = shiftNicePixels(nicepixels);
        uint8_t i3 = shiftNicePixels(nicepixels);
        uint8_t i4 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i1];
        *bmp.bmp16++ = py2[
#ifndef TIMED_COMMIT            
            color_index = 
#endif            
            i2];
        *bmp.bmp16++ = py2[i3];
        *bmp.bmp16++ = py2[i4];
    }
}

#ifdef TIMED_COMMIT
IRAM_ATTR
inline void slab8_pal()
{
    uint32_t nicepixels = fetchNicePixels();
    {
        uint8_t i1 = shiftNicePixels(nicepixels);
        uint8_t i2 = shiftNicePixels(nicepixels);
        uint8_t i3 = shiftNicePixels(nicepixels);
        uint8_t i4 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i1];
        *bmp.bmp16++ = py2[i2];
        // // if we commit with i2 here, the 8bit snail flickers, clrspace is good
        // // with i1: 8bit snail is good, clrspace is broken
        if (commit_pal) io->commit_palette(i2); 
        *bmp.bmp16++ = py2[i3];
        *bmp.bmp16++ = py2[i4];
    }
    {
        uint8_t i1 = shiftNicePixels(nicepixels);
        uint8_t i2 = shiftNicePixels(nicepixels);
        uint8_t i3 = shiftNicePixels(nicepixels);
        uint8_t i4 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i1];
        *bmp.bmp16++ = py2[i2];
        //if (commit_pal) io->commit_palette(i2); // this is where i thought it should be
        *bmp.bmp16++ = py2[i3];
        *bmp.bmp16++ = py2[i4];
    }
}
#endif

IRAM_ATTR
void vborderslab()
{
    uint32_t c = py2[border_index];
    c = c << 16 | c;
    *bmp.bmp32++ = c << 16 | c;
    *bmp.bmp32++ = c << 16 | c;
    *bmp.bmp32++ = c << 16 | c;
    #ifdef TIMED_COMMIT
    if (commit_pal) {
        io->commit_palette(border_index);
        uint32_t c = py2[border_index];
        c = c << 16 | c;
    }
    #endif
    *bmp.bmp32++ = c << 16 | c;
}


void borderslab()
{
    uint16_t c = py2[border_index];
    c = (c << 8) | c;
    *bmp.bmp16++ = c;
    *bmp.bmp32++ = c << 16 | c;
    #ifdef TIMED_COMMIT
    if (commit_pal) {
        io->commit_palette(border_index);
        uint32_t c = py2[border_index];
        c = c << 16 | c;
    }
    #endif
    *bmp.bmp32++ = c << 16 | c;
}


IRAM_ATTR
int bob(int maxframes)
{
    // have two counters: 
    //  ipixels -- instruction pixels, or cpu instruction cycles * 4
    //  rpixels -- raster pixels, 768 rpixels per raster line
    // one frame is 59904 * 4 = 239616 ipixels
    // or           768 * 312 = 239616 rpixels

    // special beam locations:
    // 0,0 -- start of frame
    // 0, 174 -- frame interrupt request
    // 10  -- first line written to buffer
    // 40  -- first bitplane line
    // 40, 150 -- scroll register (fb_row) is loaded
    // 295 -- last bitplane
    // 309 -- last line written to buffer
    // 311 -- wraparound
    // *, 24 is the first v06c pixel position

    // make buffer full width, not 532 but 768 pixels wide

    // filling the void: no reason to count individual pixels in this area
    int rpixels = 0;
    int ipixels = 0; 

    ///int commit_time = 0, commit_time_pal = 0;
    int line5 = 0;

    printf("buffers[0]=%p buffers[0].10=%p  buffers[1]=%p buffers[0].10=%p\n", buffers[0], buffers[0]+10, buffers[1], buffers[1]+10);

    for(int frm = 0; maxframes == 0 || frm < maxframes; ++frm) {
        write_buffer = 0;
        bmp.bmp8 = buffers[write_buffer];
        line5 = 5;

        //printf("frame %d\n", frm);

        // frame counted in 16-pixel chunks
        // 768/16 = 48, 0x30 -> next line when i & 0x3f == 0x30
        for (int line = 0; line < 312; ++line) {
            //if (line == 0) {
            //    irq = inte; 
            //}
            if (line == 40) {
                fb_row = io->ScrollStart(); 
            }

            int column;
            bool line_is_visible = line >= first_visible_line && line < last_visible_line;

            if (!line_is_visible) {
                for (column = 0; column < 48; ++column) {
                    if (line == 0 && column == 12) {
                        irq = inte;
                    }
                    if (ipixels <= rpixels) [[unlikely]] {
                        if (irq && i8080cpu::i8080_iff()) [[unlikely]] {
                            inte = false;
                            if (i8080cpu::last_opcode == 0x76) {
                                i8080cpu::i8080_jump(i8080cpu::i8080_pc() + 1);
                            }
                            ipixels += i8080cpu::i8080_execute(0xff); // rst7
                            //printf("interrupt\n");
                        }
                        #ifdef TIMED_COMMIT
                        commit_pal = false;
                        #endif
                        ipixels += i8080cpu::i8080_instruction(); // divisible by 4
                        #ifdef TIMED_COMMIT
                        if (commit_pal) io->commit_palette(border_index);
                        #endif
                        irq = false;
                    }
                    #ifndef TIMED_COMMIT
                    color_index = border_index; // important for commit palette
                    #endif
                    rpixels += 4;
                }
                goto rowend;
            }

            if (line < first_raster_line || line >= last_raster_line) {  // visible but no raster, vertical border
                for (column = 0; column < 48; ++column) {
                    if (ipixels <= rpixels) [[unlikely]] {
                        #ifdef TIMED_COMMIT
                        commit_pal = false;
                        #endif
                        ipixels += i8080cpu::i8080_instruction(); // divisible by 4
                    }
                    #ifndef TIMED_COMMIT
                    color_index = border_index;
                    #endif
                    if (column >= 10 && column < 42) {
                        vborderslab();
                    }
                    else if (column == 9 || column == 42) {
                        borderslab();
                    }
                    else {
                        #ifdef TIMED_COMMIT
                        if (commit_pal) io->commit_palette(border_index);
                        #endif
                    }
                    rpixels += 4;
                }
                goto rowend;
            }

            // line counted in 16-pixel columns (8 6mhz pixel columns, one v06c byte)
            /// COLUMNS 0..9
            for (column = 0; column < 10; ++column) {
                // (4, 8, 12, 16, 20, 24) * 4
                if (ipixels <= rpixels) [[unlikely]] {
                    #ifdef TIMED_COMMIT
                    commit_pal = false;
                    #endif
                    ipixels += i8080cpu::i8080_instruction(); // divisible by 4
                }
                #ifndef TIMED_COMMIT
                color_index = border_index; // important for commit palette
                #endif
                rpixels += 4;
            }
            borderslab();
            fb_column = -1;
            /// COLUMNS 10...
            for (; column < 42; ++column) {
                // (4, 8, 12, 16, 20, 24) * 4
                if (ipixels <= rpixels) [[unlikely]] {
                    #ifdef TIMED_COMMIT
                    commit_pal = false;
                    #endif
                    ipixels += i8080cpu::i8080_instruction(); // divisible by 4
                }

                //?color_index = border_index; // important for commit palette
                ++fb_column;
                #ifdef TIMED_COMMIT
                slab8_pal();
                #else
                slab8();        // will update color_index
                #endif
                rpixels += 4;
            }

            // right edge of the bitplane area
            if (ipixels <= rpixels) [[unlikely]] {
                #ifdef TIMED_COMMIT
                commit_pal = false;
                #endif
                ipixels += i8080cpu::i8080_instruction(); // divisible by 4
            }
            //?color_index = border_index;
            rpixels += 4;
            borderslab();
            ++column;

            /// COLUMNS 43.....
            for (; column < 48; ++column) {
                // (4, 8, 12, 16, 20, 24) * 4
                if (ipixels <= rpixels) [[unlikely]] {
                    #ifdef TIMED_COMMIT
                    commit_pal = false;
                    #endif
                    ipixels += i8080cpu::i8080_instruction(); // divisible by 4
                    #ifdef TIMED_COMMIT
                    if (commit_pal) io->commit_palette(border_index);
                    #endif
                }
                #ifndef TIMED_COMMIT
                color_index = border_index;
                #endif
                rpixels += 4;
            }

            //// --- columns
rowend:
            fb_row -= 1;
            if (fb_row < 0) {
                fb_row = 0xff;
            }

            if (--line5 == 0) {
                write_buffer ^= 1;
                bmp.bmp8 = buffers[write_buffer];
                line5 = 5;

                if (line > 10 && line < 310) {
                    int pos_px;
                    do {
                        xQueueReceive(::scaler_to_emu, &pos_px, portMAX_DELAY);
                    } while (line == 14 && pos_px != 0);
                }
            }
        }

        ++v06x_framecount;
        v06x_frame_cycles = ipixels;
    }

    return maxframes;
}

uint16_t * palette8()
{
    return py2;
}

}


IRAM_ATTR
void i8080_hal_io_output(int port, int value)
{
    //printf("output port %02x=%02x\n", port, value);
    esp_filler::io->output(port, value);
    #ifndef TIMED_COMMIT    
    esp_filler::io->commit_palette(0x0f & esp_filler::color_index);
    #else
    if (port >= 0xc && port <= 0xf) {
        esp_filler::commit_pal = true;
    }
    #endif
    esp_filler::io->commit();
}

void i8080_hal_iff(int on)
{
    esp_filler::inte = on;
}


