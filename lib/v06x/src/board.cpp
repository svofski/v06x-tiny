#include <stdio.h>
#include <vector>
#include <functional>
#include <algorithm>
#include "i8080.h"
#include "esp_filler.h"
#include "board.h"
#include "util.h"

#include "esp_attr.h"

extern "C" unsigned char* boots_bin;
extern "C" unsigned int boots_bin_len;

using namespace i8080cpu;

Board::Board(Memory& _memory, IO& _io, WavPlayer& _tape_player)
  : memory(_memory)
  , io(_io)
  , tape_player(_tape_player)
  , scripting(false)
  , script_interrupt(false)
{
    this->inte = false;
}

void Board::init()
{
    i8080_hal_bind(memory, io, *this);
    create_timer();
}

void Board::init_bootrom(const uint8_t* src, size_t size)
{
#if !defined(_MSC_VER)
    std::vector<uint8_t> userboot = util::load_binfile(Options.bootromfile);
    if (userboot.size() > 0) {
        printf("User bootrom: %s (%u bytes)\n", Options.bootromfile.c_str(),
          userboot.size());
        this->boot = userboot;
    } else
#endif
    {
        this->boot.resize(size);
        for (unsigned i = 0; i < size; ++i) {
            this->boot[i] = src[i];
        }
        printf("init_bootrom: size=%u\n", this->boot.size());
    }
}

void Board::set_bootrom(const std::vector<uint8_t>& bootbytes)
{
    printf("Board::set_bootrom bootbytes.size()=%u\n", bootbytes.size());
    this->boot = bootbytes;
    printf("Board::set_bootrom boot.size()=%u\n", boot.size());
}

void Board::reset(Board::ResetMode mode)
{
    switch (mode) {
        case ResetMode::BLKVVOD:
            if (this->boot.size() == 0) {
                this->init_bootrom(
                  (const uint8_t*)&boots_bin, (size_t)boots_bin_len);
            }
            this->memory.attach_boot(boot);
            printf("Board::reset() attached boot, size=%u\n",
              (unsigned int)boot.size());
            break;
        case ResetMode::BLKSBR:
            this->memory.detach_boot();
            printf("Board::reset() detached boot\n");
            break;
        case ResetMode::LOADROM:
            this->memory.detach_boot();
            i8080_jump(Options.pc);
            i8080_setreg_sp(0xc300);
            printf("Board::reset() detached boot, pc=%04x sp=%04x\n",
              i8080_pc(), i8080_regs_sp());
            break;
    }

    this->interrupt(false);
    i8080cpu::last_opcode = 0;
    total_v_cycles = 0;
    i8080_init();
}

void Board::interrupt(bool on)
{
    this->inte = on;
    this->irq &= on;
    esp_filler::irq &= on;
    esp_filler::inte = on;
}

/* Fuses together inner CPU logic and Vector-06c interrupt logic */
bool Board::check_interrupt()
{
    if (this->irq && i8080_iff()) {
        this->interrupt(false); // lower INTE which clears INT request on D65.2
        if (i8080cpu::last_opcode == 0x76) {
            i8080_jump(i8080_pc() + 1);
        }
        this->instr_time += i8080_execute(0xff); // rst7

        return true;
    }

    return false;
}
#define F1 8
#define F2 370
//#define DBG_FRM(a,b,bob) if (frame_no>=a && frame_no<=b) {bob;}
#define DBG_FRM(a, b, bob) {};


void Board::handle_event(SDL_Event& event)
{
    // printf("handle_event: event.type=%d\n", event.type);
    switch (event.type) {
        case SDL_KEYDOWN:
            this->handle_keydown(event.key);
            break;
        case SDL_KEYUP:
            this->handle_keyup(event.key);
            break;
        default:
            break;
    }
}

/* emulator thread */
void Board::handle_keydown(SDL_KeyboardEvent& key)
{
    this->io.the_keyboard().key_down(key);
}

/* emulator thread */
void Board::handle_keyup(SDL_KeyboardEvent& key)
{
    this->io.the_keyboard().key_up(key);
}

void Board::dump_memory(const int start, const int count)
{
    for (int i = start; i < start + count; ++i) {
        printf("%02x ", memory.read(i, false));
        if ((i + 1) % 16 == 0) {
            printf("\n");
        }
    }
}


// --- scripting hooks

void Board::script_attached()
{
    this->scripting = true;
    this->script_break();
}

void Board::script_detached()
{
    this->scripting = false;
    this->script_continue();
}

void Board::script_break()
{
    this->script_interrupt = true;
}

void Board::script_continue()
{
    this->script_interrupt = false;
}


static bool iospace(uint32_t addr)
{
    return (addr & 0x80000000) != 0;
}


#include "serialize.h"

void Board::serialize(std::vector<uint8_t>& to)
{
    this->memory.serialize(to);
    this->io.serialize(to);
    i8080cpu::serialize(to);
    this->serialize_self(to);
}

void Board::serialize_self(SerializeChunk::stype_t& to) const
{
    SerializeChunk::stype_t chunk;
    chunk.push_back(static_cast<uint8_t>(this->inte));
    chunk.push_back(static_cast<uint8_t>(this->irq));
    chunk.push_back(static_cast<uint8_t>(this->irq_carry));

    size_t total_v_cycles_m[1] = { total_v_cycles };
    auto total_v_cycles_p = reinterpret_cast<uint8_t*>(total_v_cycles_m);
    chunk.insert(
      std::end(chunk), total_v_cycles_p, total_v_cycles_p + sizeof(size_t));

    SerializeChunk::insert_chunk(to, SerializeChunk::BOARD, chunk);
}

void Board::deserialize_self(
  SerializeChunk::stype_t::iterator from, uint32_t size)
{
    this->inte = static_cast<bool>(*from++);
    this->irq = static_cast<bool>(*from++);
    this->irq_carry = static_cast<bool>(*from++);

    size_t total_v_cycles_m[1];
    auto total_v_cycles_p = reinterpret_cast<uint8_t*>(total_v_cycles_m);
    size_t total_v_cycles_sizeof = sizeof(size_t);

    std::copy(from, from + total_v_cycles_sizeof, total_v_cycles_p);

    total_v_cycles = total_v_cycles_m[0];
    from += total_v_cycles_sizeof;
}

bool Board::deserialize(std::vector<uint8_t>& from)
{
    auto it = from.begin();
    uint32_t size;
    bool result = true;
    for (; it != from.end();) {
        SerializeChunk::id signature;
        auto begin = SerializeChunk::take_chunk(it, signature, size);
        it = begin + size;
        if (size > 0) {
            switch (signature) {
                case SerializeChunk::MEMORY:
                    this->memory.deserialize(begin, size);
                    break;
                case SerializeChunk::IO:
                    this->io.deserialize(begin, size);
                    break;
                case SerializeChunk::CPU:
                    i8080cpu::deserialize(begin, size);
                    break;
                case SerializeChunk::BOARD:
                    this->deserialize_self(begin, size);
                    break;
                default:
                    it = from.end();
                    result = false;
                    break;
            }
        }
    }
    return result;
}

void Board::set_joysticks(int joy_0e, int joy_0f)
{
    this->io.set_joysticks(joy_0e, joy_0f);
}
