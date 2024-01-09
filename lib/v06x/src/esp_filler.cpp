#define ESP_DELAYED_COMMIT 1
#define USE_BIT_PERMUTE 1

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "esp_timer.h"
#include "esp_attr.h"
#include <cstdint>

#include "params.h"
#include "globaldefs.h"
#include "vio.h"
#include "esp_filler.h"
#include "i8080.h"
#include "AySound.h"

#include "sync.h"
#include "scaler.h"
#include "audio.h"

int audiobuf_index;
audio_sample_t * audio_buf;


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
volatile int border_index;
int fiveline_count;
#ifndef TIMED_COMMIT
int color_index;    // index of color at the tip of the beam
#endif

#ifdef TIMED_COMMIT
bool commit_pal;
int commit_io;
uint8_t palette_byte;
#endif

volatile int v06x_framecount = 0;
volatile int v06x_frame_cycles = 0;

volatile int usrus_holdframes = 0;

std::function<void(ResetMode)> onreset;
std::function<void(void)> onosd;

IO * io;
I8253 * vi53;

// palette ram
// index:4 -> {rgb,rgb}
uint16_t py2[16];

uint16_t py2_512[16];       // mode512 pairs of pixels
uint16_t py2_256[16];       // mode256 pairs of pixels

void write_pal(uint8_t adr8, uint8_t rgb)
{
    // write 256-pixel pal
    py2_256[adr8] = (rgb << 8) | rgb;
    // 512-pixel pal (different odd/even columns)
    if (adr8 <= 3) {
        // even columns 0, 2, ...
        uint8_t adr = adr8 & 0x03;
        // replicate for every combo of msb in lsb pixel
        py2_512[adr + 0x0] = (py2_512[adr + 0x0] & 0xff00) | rgb;
        py2_512[adr + 0x4] = (py2_512[adr + 0x4] & 0xff00) | rgb;
        py2_512[adr + 0x8] = (py2_512[adr + 0x8] & 0xff00) | rgb;
        py2_512[adr + 0xc] = (py2_512[adr + 0xc] & 0xff00) | rgb;
    }
    if ((adr8 & 3) == 0) {
        // odd columns 1, 3, ...
        uint8_t adr = adr8 & 0x0c;
        uint16_t rgbshift = rgb << 8;
        // replicate for every combo of lsb in msb pixel
        py2_512[adr + 0x0] = (py2_512[adr + 0x0] & 0x00ff) | rgbshift;
        py2_512[adr + 0x1] = (py2_512[adr + 0x1] & 0x00ff) | rgbshift;
        py2_512[adr + 0x2] = (py2_512[adr + 0x2] & 0x00ff) | rgbshift;
        py2_512[adr + 0x3] = (py2_512[adr + 0x3] & 0x00ff) | rgbshift;
    }
}

void modechange()
{
    if (mode512) {
        memcpy(py2, py2_512, 16 * 2);
    }
    else {
        memcpy(py2, py2_256, 16 * 2);
    }
}

void print_palette()
{
    for (int i = 0; i < 16; ++i) {
        printf("[%d]=%04x ", i, py2_512[i]);
    }
    printf("\n");
}

void commit_palette(int index) 
{
    esp_filler::write_pal(index, palette_byte);
    modechange();
    //printf("[%02x]=%02x ", index, palette_byte);
}

void init(uint32_t * _mem32, IO * _io, uint8_t * buf1, uint8_t * buf2, I8253 * _vi53)
{
    mem32 = &_mem32[0x2000]; // pre-offset 
    io = _io;
    buffers[0] = buf1;
    buffers[1] = buf2;
    write_buffer = 0;    
    bmp.bmp8 = buffers[0];
    fiveline_count = 0;        // count groups of 5 lines

    audiobuf_index = 0;
    audio_buf = audio::audio_pp[audiobuf_index];
    AySound::SamplebufAY = audio::ay_pp[audiobuf_index];

    io->onborderchange = [](int border) {
        border_index = border;
    };

    io->onmodechange = [](bool mode) {
        mode512 = mode;
        modechange();
    };

    vi53 = _vi53;

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
        if (commit_pal) commit_palette(i2); 
        // // if we commit with i2 here, the 8bit snail flickers, clrspace is good
        // // with i1: 8bit snail is good, clrspace is broken
        if (commit_io && --commit_io == 0) {
            io->commit();
        }
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
        //if (commit_pal) io->commit_palette(i3); // this is where i thought it should be
        *bmp.bmp16++ = py2[i4];
    }
}
#endif

// full column slab in the vertical border area
IRAM_ATTR
void vborderslab()
{
    uint32_t c = py2[border_index];
    *bmp.bmp32++ = c << 16 | c;
    if (commit_pal) {
        commit_palette(border_index);
        c = py2[border_index];
    }
    *bmp.bmp32++ = c << 16 | c;
    if (commit_io && --commit_io == 0) {
        io->commit();
        c = py2[border_index];
    }
    *bmp.bmp32++ = c << 16 | c;
    *bmp.bmp32++ = c << 16 | c;
}

// an edge border slab, to be eliminated in favour of full width border later
IRAM_ATTR
void borderslab()
{
    uint16_t c = py2[border_index];
    if (commit_pal) {
        commit_palette(border_index);
        c = py2[border_index];
    }
    *bmp.bmp16++ = c;
    #ifdef TIMED_COMMIT
    if (commit_io && --commit_io == 0) {
        io->commit();
        c = py2[border_index];
    }
    #endif
    *bmp.bmp32++ = c << 16 | c;
    *bmp.bmp32++ = c << 16 | c;
}

int rpixels;
int last_rpixels;
int frame_rpixels;
int ay_bufpos, ay_bufpos_reg;

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

    // timer works at 1.5mhz, 1/8 pixel clock
    // 768/8 = 96 timer clocks per line, or 2 timer clocks per column```````````

    // filling the void: no reason to count individual pixels in this area
    rpixels = last_rpixels = frame_rpixels = 0;
    int ipixels = 0; 

    ///int commit_time = 0, commit_time_pal = 0;
    int line6 = 0;
    ay_bufpos_reg = 0;

    //printf("buffers[0]=%p buffers[0].10=%p  buffers[1]=%p buffers[0].10=%p\n", buffers[0], buffers[0]+10, buffers[1], buffers[1]+10);

    for(int frm = 0; maxframes == 0 || frm < maxframes; ++frm) {
        write_buffer = 0;
        bmp.bmp8 = buffers[write_buffer];
        line6 = 6;
        frame_rpixels = rpixels;
        ay_bufpos = 0;
        //printf("frame %d\n", frm);
        // frame counted in 16-pixel chunks
        // 768/16 = 48, 0x30 -> next line when i & 0x3f == 0x30
        for (int line = 0; line < 312; ++line) {
            int column;
            bool line_is_visible = line >= first_visible_line && line <= last_visible_line;
            // invisible 
            if (!line_is_visible) {
                for (column = 0; column < 48; ++column) {
                    if (line == 0 && column == 9) {
                        irq = inte;
                    }
                    if (ipixels <= rpixels) [[unlikely]] {
                        if (irq && i8080cpu::i8080_iff()) [[unlikely]] {
                            inte = false;
                            if (i8080cpu::last_opcode == 0x76) {
                                i8080cpu::i8080_jump(i8080cpu::i8080_pc() + 1);
                            }
                            ipixels += i8080cpu::i8080_execute(0xff); // rst7
                        }
                        #ifdef TIMED_COMMIT
                        commit_pal = false;
                        #endif
                        ipixels += i8080cpu::i8080_instruction(); // divisible by 4
                        #ifdef TIMED_COMMIT
                        if (commit_pal) commit_palette(border_index);
                        if (commit_io && --commit_io == 0) {
                            io->commit();
                        }
                        #endif
                        irq = false;
                    }
                    #ifndef TIMED_COMMIT
                    color_index = border_index; // important for commit palette
                    #endif
                    rpixels += 4;

                    if (column == 24) {
                        // update passed vi53 counts and save last_rpixels, last_rpixels can also be updated in io_output
                        vi53->count_clocks((rpixels - last_rpixels) >> 1); // 96 timer clocks per line
                        last_rpixels = rpixels;
                        *audio_buf++ = vi53->out_sum() << AUDIO_SCALE_8253;   // sample audio
                    }
                }
                goto rowend;
            }

            // if (line == first_visible_line) {
            //     if (frm == 150) {
            //         i8080cpu::trace_enable = 1;
            //     }
            // }

            // visible but no raster, vertical border
            if (line < first_raster_line || line >= last_raster_line) {  
                for (column = 0; column < 48; ++column) {
                    #if DEBUG_INSTRUCTION_STRIPES
                    bool xoxo = false;
                    #endif
                    if (ipixels <= rpixels) [[unlikely]] {
                        #ifdef TIMED_COMMIT
                        commit_pal = false;
                        #endif
                        //if (i8080cpu::trace_enable) printf("c=%d rpixel=%d ", column, rpixels - rpixel0);
                        ipixels += i8080cpu::i8080_instruction(); // divisible by 4
                        #if DEBUG_INSTRUCTION_STRIPES
                        xoxo = true;
                        #endif
                    }
                    #ifndef TIMED_COMMIT
                    color_index = border_index;
                    #endif
                    if (column >= 10 && column < 42) {
                        vborderslab();
                        #if DEBUG_INSTRUCTION_STRIPES
                        if (xoxo) {
                            if (i8080cpu::last_opcode == 0x76) {
                                *(bmp.bmp8 - 16) = write_buffer ? (0x07<<3) : 0xc0;
                            } else
                                *(bmp.bmp8 - 16) = 0x07;
                        }
                        #endif
                    }
                    else if (column == 9 || column == 42) {
                        borderslab();
                    }
                    else {
                        #ifdef TIMED_COMMIT
                        if (commit_pal) commit_palette(border_index);
                        if (commit_io && --commit_io == 0) {
                            io->commit();
                        }
                        #endif
                    }
                    rpixels += 4;

                    if (column == 24) {
                        // update passed vi53 counts and save last_rpixels, last_rpixels can also be updated in io_output
                        vi53->count_clocks((rpixels - last_rpixels) >> 1); // 96 timer clocks per line
                        last_rpixels = rpixels;
                        *audio_buf++ = vi53->out_sum() << AUDIO_SCALE_8253;   // sample audio
                    }

                }
                goto rowend;
            }

            //i8080cpu::trace_enable = 0;
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

            if (line == 40) {
                fb_row = io->ScrollStart(); 
            }

            borderslab();
            fb_column = -1;
            /// COLUMNS 10...41
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

                if (column == 24) {
                    // update passed vi53 counts and save last_rpixels, last_rpixels can also be updated in io_output
                    vi53->count_clocks((rpixels - last_rpixels) >> 1); // 96 timer clocks per line
                    last_rpixels = rpixels;
                    *audio_buf++ = vi53->out_sum() << AUDIO_SCALE_8253;   // sample audio
                }
            }

            // COLUMN 42: right edge of the bitplane area
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
                    if (commit_pal) commit_palette(border_index);
                    if (commit_io && --commit_io == 0) {
                        io->commit();
                    }
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

            if (--line6 == 0) {
                // ay line update
                {
                    size_t bufpos = (esp_filler::rpixels - esp_filler::frame_rpixels) / 96;
                    if (bufpos > esp_filler::ay_bufpos) {
                        //printf("gen-c: bufpos=%d aypos=%d cnt=%d!\n", bufpos, esp_filler::ay_bufpos, bufpos - esp_filler::ay_bufpos);
                        AySound::gen_sound(bufpos - esp_filler::ay_bufpos, esp_filler::ay_bufpos);
                        esp_filler::ay_bufpos = bufpos;
                    }
                }

                write_buffer ^= 1;
                bmp.bmp8 = buffers[write_buffer];
                line6 = 6;

                if (line > first_visible_line) {// && line < last_visible_line) {
                    int pos_px;
                    do {
                        xQueueReceive(::scaler_to_emu, &pos_px, portMAX_DELAY);
                    } while (line == first_visible_line + 6 - 1 && pos_px != 0);
                }
            }

            // update passed vi53 counts and save last_rpixels, last_rpixels can also be updated in io_output
            vi53->count_clocks((rpixels - last_rpixels) >> 1); // 96 timer clocks per line
            last_rpixels = rpixels;
            *audio_buf++ = vi53->out_sum() << AUDIO_SCALE_8253;   // sample audio
        }
        keyboard::io_commit_ruslat();
        keyboard::io_read_modkeys(); 

        if ((keyboard::state.pc & keyboard::PC_MODKEYS_MASK) == ((keyboard::PC_BIT_US | keyboard::PC_BIT_RUSLAT) ^ keyboard::PC_MODKEYS_MASK)) {
            usrus_holdframes = usrus_holdframes + 1;
            if (usrus_holdframes == OSD_USRUS_FRAMES_HOLD && onosd) {
                onosd();
            }
        }
        else {
            usrus_holdframes = 0;
        }

        ay_bufpos_reg = ay_bufpos;
        // post audio buffer index to be taken in by the audio driver
        xQueueSend(::audio_queue, &audiobuf_index,  5 / portTICK_PERIOD_MS);
        if (++audiobuf_index == AUDIO_NBUFFERS) audiobuf_index = 0;
        audio_buf = audio::audio_pp[audiobuf_index];
        AySound::SamplebufAY = audio::ay_pp[audiobuf_index];

        int cmd;
        if (xQueueReceive(::emu_command_queue, &cmd, 0)) {
            if (cmd == CMD_EMU_BREAK) {
                break;
            }
        }

        if (keyboard::sbros_pressed()) {
            onreset(ResetMode::BLKSBR);
        }
        else if (keyboard::vvod_pressed()) {
            onreset(ResetMode::BLKVVOD);
        }

        v06x_framecount = v06x_framecount + 1;
        v06x_frame_cycles = ipixels;
    }

    return maxframes;
}

}


// as per v06x:
//   port 2 commit time: 8 * 4 (32 pixels)
//   port c commit time: ?     (12 pixels)

// new idea:
//   i8080 doesn't hal out out instruction until the next one -- that's fairly close to the real thing
//   commit of all ports except 02 is immediate
//   commit to port 2 is delayed by ~ 1 column
IRAM_ATTR
void i8080_hal_io_output(int port, int value)
{
    //if (port < 16) printf("output port %02x=%02x\n", port, value);
    esp_filler::io->output(port, value);
    #ifndef TIMED_COMMIT    
    esp_filler::io->commit_palette(0x0f & esp_filler::color_index);
    #else
    // non-palette i/o
    if (port >= 0x15 || port == 0x10) {
        esp_filler::io->commit();           // all regular peripherals
    }
    else if (port == 0x14) {
        // generate ay sound up to current position
        size_t bufpos = (esp_filler::rpixels - esp_filler::frame_rpixels) / 96;
        if (bufpos > esp_filler::ay_bufpos) {
            AySound::gen_sound(bufpos - esp_filler::ay_bufpos, esp_filler::ay_bufpos);
            esp_filler::ay_bufpos = bufpos;
        }
        esp_filler::io->commit();           // all regular peripherals
    }
    else if (port <= 0xb) {        
        if (port > 0x08) {  // timer
            esp_filler::vi53->count_clocks((esp_filler::rpixels - esp_filler::last_rpixels) >> 1); // 96 timer clocks per line
            esp_filler::last_rpixels = esp_filler::rpixels;
        }
        esp_filler::io->commit();           // all regular peripherals
    }
    else if (port >= 0xc && port <= 0xf) {
        esp_filler::commit_pal = true;      // near-instant
        esp_filler::palette_byte = value;
    }
    else {
        esp_filler::commit_io = 2;          // border updates with delay
    }
    #endif
    //esp_filler::io->commit();
}

void i8080_hal_iff(int on)
{
    esp_filler::inte = on;
}


