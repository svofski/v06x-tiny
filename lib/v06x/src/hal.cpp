#include <stdio.h>
#include <stdint.h>
#include "i8080.h"
#include "i8080_hal.h"
#include "memory.h"
#include "vio.h"
#include "board.h"
#if !defined(__ANDROID_NDK__) && !defined(__GODOT__) && !defined(ESP_PLATFORM)
#include "SDL.h"
#endif

static Memory * memory;
static IO * io;
static Board * board;

void i8080_hal_bind(Memory & _mem, IO & _io, Board & _board)
{
    memory = &_mem;
    io = &_io;
    board = &_board;
}

IRAM_ATTR
int i8080_hal_memory_read_byte(int addr, const bool _is_opcode)
{
    return memory->read(addr, false, _is_opcode);
}

IRAM_ATTR
void i8080_hal_memory_write_byte(int addr, int value)
{
    return memory->write(addr, value, false);
}

IRAM_ATTR
int i8080_hal_memory_read_word(int addr, bool stack)
{
    //return memory->read(addr, stack) | (memory->read(addr+1, stack) << 8);
    uint16_t tmp = (memory->read(addr + 1, stack) << 8);
    tmp |= memory->read(addr, stack);

    // if (i8080cpu::i8080_pc() == 0x5a8) {
    //     printf("0x5a7 pop d: sp=%x de=%04x\n", (uint16_t)addr,  tmp);
    // }

    return tmp;
}

IRAM_ATTR
void i8080_hal_memory_write_word(int addr, int word, bool stack)
{
    memory->write(addr, word & 0377, stack);
    memory->write(addr + 1, word >> 8, stack);
}

//IRAM_ATTR
//int i8080_hal_io_input(int port)
//{
//    int value = io->input(port);
//    //printf("input port %02x = %02x\n", port, value);
//    return value;
//}

// IRAM_ATTR
// void i8080_hal_io_output(int port, int value)
// {
//     //printf("output port %02x=%02x\n", port, value);
//     io->output(port, value);
// }

// void i8080_hal_iff(int on)
// {
//     board->interrupt(on);
// }

uint32_t timer_callback(uint32_t interval, void * param)
{
    board->onframetimer();
    return(interval);
}

/* If there is no audio buffer to drive the frame rate, use the timer */
void create_timer()
{
    if (Options.nosound && Options.novideo) {
        /* Used in tests, event loop kick-spins itself without timers. */
        return;
    }
#if !defined(__ANDROID_NDK__) && !defined(__GODOT__) && !defined(ESP_PLATFORM)
    if (Options.nosound) {
        printf("create_timer(): nosound is set, will use SDL timer for frames\n");
        SDL_Init(SDL_INIT_TIMER);
        uint32_t period = 1000 / 50;
        SDL_TimerID sometimer = SDL_AddTimer(period, timer_callback, NULL);
        if (sometimer == 0) {
            fprintf(stderr, "SDL_AddTimer %s\n", SDL_GetError());
        }
    }
#endif
}
