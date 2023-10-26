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
uint8_t color_index;    // index of color at the tip of the beam

IO * io;

// palette ram
uint16_t py2[16];

void init(uint32_t * _mem32, IO * _io, uint8_t * buf1, uint8_t * buf2)
{
    mem32 = _mem32;
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
    uint32_t x = mem32[0x2000 + addr];
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
inline int shiftOutPixels()
{
#if USE_BIT_PERMUTE
    uint32_t modeless = pixel32_grouped >> 28;
    pixel32_grouped <<= 4;
#else
    uint32_t p = pixel32;
    // msb of every byte in p stands for bit plane
    uint32_t modeless = (p >> 4 & 8) | (p >> 13 & 4) | (p >> 22 & 2) | (p >> 31 & 1);
    // shift left
    pixel32 = (p << 1);// & 0xfefefefe; -- unnecessary
#endif
    return modeless;
}



IRAM_ATTR
void slab8()
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
        color_index = *bmp.bmp16++ = py2[i4];
    }
    {
        uint8_t i1 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i1];
        uint8_t i2 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i2];
        uint8_t i3 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i3];
        uint8_t i4 = shiftNicePixels(nicepixels);
        *bmp.bmp16++ = py2[i4];
    }
}

void borderslab()
{
    uint16_t c = py2[border_index];
    c = (c << 8) | c;
    *bmp.bmp16++ = c;
    *bmp.bmp16++ = c;
    *bmp.bmp16++ = c;
    *bmp.bmp16++ = c;
    *bmp.bmp16++ = c;
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

    int commit_time = 0, commit_time_pal = 0;
    int line5 = 0;

    printf("buffers[0]=%p buffers[0].10=%p  buffers[1]=%p buffers[0].10=%p\n", buffers[0], buffers[0]+10, buffers[1], buffers[1]+10);

    for(int frm = 0; maxframes == 0 || frm < maxframes; ++frm) {
        write_buffer = 0;
        bmp.bmp8 = buffers[write_buffer];
        line5 = 5;

        int pos_px;
        // while (xQueueReceive(scaler_to_emu, &pos_px, portMAX_DELAY)) {
        //     if (pos_px == 0) {
        //         break;
        //     }
        // }

        // frame counted in 16-pixel chunks
        // 768/16 = 48, 0x30 -> next line when i & 0x3f == 0x30
        for (int line = 0; line < 312; ++line) {
            if (line == 0) {
                irq = true;
            }
            if (line == 40) {
                fb_row = io->ScrollStart(); 
            }

            if (true) {//line >= first_visible_line && line < last_visible_line) {
                // line counted in 16-pixel columns (8 6mhz pixel columns, one v06c byte)
                /// COLUMNS 0..9
                int column;
                for (column = 0; column < 10; ++column) {
                    // (4, 8, 12, 16, 20, 24) * 4
                    if (ipixels <= rpixels) [[unlikely]] {
                        if (irq && i8080cpu::i8080_iff()) [[unlikely]]{
                            irq = false;
                            if (i8080cpu::last_opcode == 0x76) {
                                i8080cpu::i8080_jump(i8080cpu::i8080_pc() + 1);
                            }
                            ipixels += i8080cpu::i8080_execute(0xff) << 2; // rst7
                        }
                        ipixels += i8080cpu::i8080_instruction() << 2; // divisible by 16
                        
                        //printf("%04x %02x\n", i8080cpu::i8080_pc(), i8080cpu::last_opcode);

                        if (i8080cpu::last_opcode == 0xd3) {
                            io->commit_palette(color_index & 15);
                            io->commit();
                        }
                    }

                    color_index = border_index;
                    if (line >= first_visible_line && line < last_visible_line) {
                        if (column == 9) {
                            fb_column = -2;
                        }
                        ++fb_column;
                        if (fb_column < 0) {
                            borderslab();
                        }
                    }
                    rpixels += 16;
                    ipixels -= 16;
                }
                /// COLUMNS 10...
                for (; column < 42; ++column) {
                    // (4, 8, 12, 16, 20, 24) * 4
                    if (ipixels <= rpixels) [[unlikely]] {
                        ipixels += i8080cpu::i8080_instruction() << 2; // divisible by 16
                        if (i8080cpu::last_opcode == 0xd3) {
                            io->commit_palette(color_index & 15);
                            io->commit();
                        }
                    }

                    color_index = border_index;
                    if (line >= first_visible_line && line < last_visible_line) {
                        ++fb_column;
                        slab8();        // will update color_index
                    }
                    rpixels += 16;
                    ipixels -= 16;
                }

                /// COLUMNS 43.....
                for (; column < 48; ++column) {
                    // (4, 8, 12, 16, 20, 24) * 4
                    if (ipixels <= rpixels) [[unlikely]] {
                        ipixels += i8080cpu::i8080_instruction() << 2; // divisible by 16
                        if (i8080cpu::last_opcode == 0xd3) {
                            io->commit_palette(color_index & 15);
                            io->commit();
                        }
                    }

                    color_index = border_index;
                    if (line >= first_visible_line && line < last_visible_line) {
                        ++fb_column;
                        if (fb_column < 33)
                        {
                            borderslab();
                        }
                    }
                    rpixels += 16;
                    ipixels -= 16;
                }
            }
            else {  // invisible line
                for (int column = 0; column < 48; ++column) {
                    // (4, 8, 12, 16, 20, 24) * 4
                    if (ipixels <= rpixels) [[unlikely]] {
                        if (irq && i8080cpu::i8080_iff()) [[unlikely]]{
                            irq = false;
                            if (i8080cpu::last_opcode == 0x76) {
                                i8080cpu::i8080_jump(i8080cpu::i8080_pc() + 1);
                            }
                            ipixels += i8080cpu::i8080_execute(0xff) << 2; // rst7
                        }
                        ipixels += i8080cpu::i8080_instruction() << 2; // divisible by 16
                        
                        //printf("%04x %02x\n", i8080cpu::i8080_pc(), i8080cpu::last_opcode);

                        if (i8080cpu::last_opcode == 0xd3) {
                            io->commit_palette(color_index & 15);
                            io->commit();
                        }
                    }

                    color_index = border_index;
                    rpixels += 16;
                    ipixels -= 16;
                }                
            }

            //// --- columns

            fb_row -= 1;
            if (fb_row < 0) {
                fb_row = 0xff;
            }

            if (--line5 == 0) {
                write_buffer ^= 1;
                bmp.bmp8 = buffers[write_buffer];
                line5 = 5;

                if (line > 10 && line < 310) {
                    do {
                        xQueueReceive(::scaler_to_emu, &pos_px, portMAX_DELAY);
                        //if (line == 14) printf("line=%d pos_px=%d\n", line, pos_px);
                    } while (line == 14 && pos_px != 0);
                    //if (line == 14) printf("sync !!!\n");
                }
            }
        }

        ++v06x_framecount;
    }

    return maxframes;
}

uint16_t * palette8()
{
    return py2;
}

}




#if 0
IRAM_ATTR
void fetchPixels() 
{
    size_t addr = ((fb_column & 0xff) << 8) | (fb_row & 0xff);
    pixel32 = mem32[0x2000 + addr];

#if USE_BIT_PERMUTE
    // h/t Code generator for bit permutations
    // http://programming.sirrida.de/calcperm.php
    // Input:
    // 31 23 15 7 30 22 14 6 29 21 13 5 28 20 12 4 27 19 11 3 26 18 10 2 25 17 9 1  24 16 8 0
    // LSB, indices refer to source, Beneš/BPC
    uint32_t x = pixel32;
    x = bit_permute_step(x, 0x00550055, 9);  // Bit index swap+complement 0,3
    x = bit_permute_step(x, 0x00003333, 18);  // Bit index swap+complement 1,4
    x = bit_permute_step(x, 0x000f000f, 12);  // Bit index swap+complement 2,3
    x = bit_permute_step(x, 0x000000ff, 24);  // Bit index swap+complement 3,4

    pixel32_grouped = x;
#endif
}

IRAM_ATTR
inline int getColorIndex(int rpixel, bool border)
{
    if (border) {
        fb_column = 0;
        return border_index;
    } else {
        if ((rpixel & 0x0f) == 0) {
            fetchPixels();
            ++fb_column;
        }
        return shiftOutPixels();
    }
}

IRAM_ATTR
void advanceLine()
{
    raster_pixel = 0;
    raster_line += 1;
    fb_row -= 1;
    if (!vborder && fb_row < 0) {
        fb_row = 0xff;
    }
    // update vertical border only when line changes
    vborder = (raster_line < 40) || (raster_line >= (40 + 256));
    // turn on pixel copying after blanking area
    visible = visible || (raster_line == first_visible_line);

    // five-line blocks
    ++fiveline_count;
    if (fiveline_count == 5) {
        fiveline_count = 0;
        write_buffer ^= 1;
        bmp = buffers[write_buffer];
    }

    if (raster_line == 312) {
        raster_line = 0;

        fiveline_count = 0;
        write_buffer = 0;
        bmp = buffers[write_buffer];

        //printf("advanceLine: fb_row=%d\n", fb_row);

        visible = false; // blanking starts
    }
}

IRAM_ATTR
int fill(int clocks, int commit_time, int commit_time_pal) 
{
    int clk;
    int index = 0;

    constexpr int raster_hstart = center_offset;
    constexpr int raster_hend = screen_width + center_offset;

    for (clk = 0; clk < clocks; clk += 2) {
        // offset for matching border/palette writes and the raster -- test:bord2
        int rpixel = raster_pixel - 24;
        bool raster_area = raster_pixel >= raster_hstart && raster_pixel < raster_hend;
        index = getColorIndex(rpixel, vborder || !raster_area);

        #if ESP_DELAYED_COMMIT
        if (clk == commit_time) {
            io->commit(); // regular i/o writes (border index); test: bord2
        }
        else if (clk == commit_time_pal) {
            io->commit_palette(index); // palette writes; test: bord2
        }
        #endif

        if (raster_area) {
            // if (mode512) {// && !border -- border A/B alternation, see Cherezov page 7
            //     *bmp++ = py2[index & 0x03];
            //     *bmp++ = py2[index & 0x0c];
            // } 
            // else 
            {
                uint8_t p = py2[index];
                *bmp++ = p;
                *bmp++ = p;
            }
        }

        // 22 vsync + 18 border + 256 picture + 16 border = 312 lines
        raster_pixel += 2;
        if (raster_pixel == 768) {
            advanceLine();
        }
        // load scroll register at this precise moment -- test:scrltst2
        if (raster_line == 22 + 18 && raster_pixel == 150) {
            fb_row = io->ScrollStart();
            // printf("load fb_row=%d\n", fb_row);
        }
        // // irq time -- test:bord2, vst (MovR=1d37, MovM=1d36)
        // else if (raster_line == 0 && raster_pixel == 174) {
        //     irq = true;
        //     irq_clk = clk;
        // }
    }

    #if ESP_DELAYED_COMMIT
    if (clk == commit_time) {
        io->commit(); // regular i/o writes (border index); test: bord2
    }
    else if (clk == commit_time_pal) {
        io->commit_palette(index); // palette writes; test: bord2
    }
    #endif

    return 1;
}

IRAM_ATTR
int fill_noout_0(int clocks) 
{
    int clk;

    int px = raster_pixel - 24;
    raster_pixel += clocks;
    uint32_t w;

    for (clk = 0; clk < clocks; clk += 8) {
        uint8_t p1 = getColorIndex(px, false); px += 2;
        uint8_t p2 = getColorIndex(px, false); px += 2;
        uint8_t p3 = getColorIndex(px, false); px += 2;
        uint8_t p4 = getColorIndex(px, false); px += 2;
        // uint8_t p5 = getColorIndex(px, false); px += 2;
        // uint8_t p6 = getColorIndex(px, false); px += 2;
        // uint8_t p7 = getColorIndex(px, false); px += 2;
        // uint8_t p8 = getColorIndex(px, false); px += 2;

        p1 = py2[p1];
        p2 = py2[p2];
        p3 = py2[p3];
        p4 = py2[p4];
        // p5 = py2[p5];
        // p6 = py2[p6];
        // p7 = py2[p7];
        // p8 = py2[p8];

        *reinterpret_cast<uint16_t *>(bmp) = (p1 << 8) | p1;
        bmp += 2;
        *reinterpret_cast<uint16_t *>(bmp) = (p2 << 8) | p2;
        bmp += 2;
        *reinterpret_cast<uint16_t *>(bmp) = (p3 << 8) | p3;
        bmp += 2;
        *reinterpret_cast<uint16_t *>(bmp) = (p4 << 8) | p4;
        bmp += 2;

        // *reinterpret_cast<uint16_t *>(bmp) = (p5 << 8) | p5;
        // bmp += 2;
        // *reinterpret_cast<uint16_t *>(bmp) = (p6 << 8) | p6;
        // bmp += 2;
        // *reinterpret_cast<uint16_t *>(bmp) = (p7 << 8) | p7;
        // bmp += 2;
        // *reinterpret_cast<uint16_t *>(bmp) = (p8 << 8) | p8;
        // bmp += 2;

    }
    return 1;
}


IRAM_ATTR
int fill_noout(int clocks) 
{
    int clk;
    int index = 0;
    // clocks can be (4..24) * 4 = 16..96
    if ((raster_line != 40) &&
        (raster_pixel >= raster_hstart && raster_pixel < raster_hend) &&
        (raster_pixel + clocks < raster_hend)) {
       return fill_noout_0(clocks);
    }

    int px = raster_pixel;
    raster_pixel += clocks;
    for (clk = 0; clk < clocks; clk += 8) {
        // offset for matching border/palette writes and the raster -- test:bord2
        bool raster_area = px >= raster_hstart && px < raster_hend;
        
        if (raster_area) {
            index = getColorIndex(px - 24, !raster_area);
            uint8_t p = py2[index];
            *reinterpret_cast<uint16_t *>(bmp) = (p << 8) | p;
            bmp += 2;
        }
        px += 2;
        //--
        if (raster_area) {
            index = getColorIndex(px - 24, !raster_area);
            uint8_t p = py2[index];
            *reinterpret_cast<uint16_t *>(bmp) = (p << 8) | p;
            bmp += 2;
        }
        px += 2;
    }
    // // load scroll register at this precise moment -- test:scrltst2
    if (raster_line == 40 && raster_pixel >= 150) {
         fb_row = io->ScrollStart();
    }

    // GAMBLE: advance line after?
    if (raster_pixel >= 768) {
        advanceLine();
    }

    return 1;
}


IRAM_ATTR
int fake_fill(int ncycles, int commit_time, int commit_time_pal)
{
    for (int i = 0; i < 32; ++i) {
        *bmp++ = i + raster_line;
    }
    advanceLine();
    advanceLine();
    advanceLine();
    advanceLine();
    advanceLine();

    return 1;
}


IRAM_ATTR
int fill_void(int clocks, int commit_time, int commit_time_pal) 
{
    int clk;
    int index = 0;

    for (clk = 0; clk < clocks; clk += 4) {
        // offset for matching border/palette writes and the raster -- test:bord2
        const int rpixel = raster_pixel - 24;
        bool border = vborder || 
            /* hborder */ (rpixel < (768-512)/2) || (rpixel >= (768 - (768-512)/2));

        index = getColorIndex(rpixel, border);        
        #if ESP_DELAYED_COMMIT
        if (clk == commit_time) {
            io->commit(); // regular i/o writes (border index); test: bord2
        }
        else if (clk == commit_time_pal) {
            io->commit_palette(index); // palette writes; test: bord2
        }
        #endif

        // 22 vsync + 18 border + 256 picture + 16 border = 312 lines
        raster_pixel += 4;
        if (raster_pixel == 768) {
            advanceLine();
        }
        // // load scroll register at this precise moment -- test:scrltst2
        // if (raster_line == 22 + 18 && raster_pixel == 150) {
        //     fb_row = io->ScrollStart();
        // }
        //else 
        // irq time -- test:bord2, vst (MovR=1d37, MovM=1d36)
        if (raster_line == 0 && raster_pixel == 172 /*174*/) {
            irq = true;
            irq_clk = clk;
        }
    } 

    #if ESP_DELAYED_COMMIT
    if (clk == commit_time) {
        io->commit(); // regular i/o writes (border index); test: bord2
    }
    else if (clk == commit_time_pal) {
        io->commit_palette(index); // palette writes; test: bord2
    }
    #endif

    return 1;
}

IRAM_ATTR
int fill_void_noout(int clocks) 
{
    int clk;
    int index = 0;

    for (clk = 0; clk < clocks; clk += 4) {
        // offset for matching border/palette writes and the raster -- test:bord2
        const int rpixel = raster_pixel - 24;
        bool border = vborder || 
            /* hborder */ (rpixel < (768-512)/2) || (rpixel >= (768 - (768-512)/2));

        index = getColorIndex(rpixel, border);        
        // 22 vsync + 18 border + 256 picture + 16 border = 312 lines
        raster_pixel += 4;
        if (raster_pixel == 768) {
            advanceLine();
        }
        // irq time -- test:bord2, vst (MovR=1d37, MovM=1d36)
        if (raster_line == 0 && raster_pixel == 172 /*174*/) {
            irq = true;
            irq_clk = clk;
        }
    } 
    return 1;
}


#endif
