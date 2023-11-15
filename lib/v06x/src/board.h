#pragma once

#include <stdio.h>
#include <vector>
#include <functional>
#include "i8080.h"
#include "serialize.h"
#include "globaldefs.h"

class Board;

void i8080_hal_bind(Memory& _mem, IO& _io, Board& _board);
void create_timer();

class Board
{
  private:
    int between;
    int instr_time;
    size_t total_v_cycles;
    int last_opcode;
    int frame_no;

    bool irq;
    bool inte;      /* CPU INTE pin */
    bool irq_carry; /* imitates cpu waiting after T2 when INTE */

    Memory& memory;
    IO& io;
    WavPlayer& tape_player;

    std::vector<uint8_t> boot;

    // script specific hooks, independent of debugger hooks
    bool scripting;         // check hooks on every instruction
    bool script_interrupt;  // paused execution because of script hook

  public:
    std::function<void(void)> onframetimer;

    struct
    {
        std::function<void(int)> frame;
        std::function<void(int)> jump;
    } hooks;

    // a hack to pass return value from io.onread
    int ioread;

  private:
    void init_bootrom(const uint8_t* src, size_t size);

  public:
    Board(Memory& _memory, IO& _io, WavPlayer& _tape_player);

    void init();
    void reset(ResetMode blkvvod); // true: power-on reset, false: boot loaded prog
    int get_frame_no() const { return frame_no; }
    void handle_quit();
    void interrupt(bool on);

    void set_joysticks(int joy_0e, int joy_0f);

  public:
    void script_attached();   // script on, begin checking hooks
    void script_detached();   // script off
    void script_break();      // break execution from script
    void script_continue();   // continue execution from script

    void serialize(std::vector<uint8_t>& to);
    bool deserialize(std::vector<uint8_t>& from);
    void serialize_self(SerializeChunk::stype_t& to) const;
    void deserialize_self(
      SerializeChunk::stype_t::iterator from, uint32_t size);

    void set_bootrom(const std::vector<uint8_t>& bootbytes);

  private:
    /* Fuses together inner CPU logic and Vector-06c interrupt logic */
    bool check_interrupt();
    int execute_frame(bool update_screen);
    bool cadence_allows();
    void dump_memory(const int start, const int count);
};

