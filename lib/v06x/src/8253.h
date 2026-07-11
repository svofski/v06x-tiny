#pragma once

#include <cstdint>
#include <functional>

#include "esp_attr.h"
#include "params.h"

class __attribute__((aligned(4))) CounterUnit
{
    friend class TestOfCounterUnit;

    // keep all fields 32-bit aligned
    int latch_value;
    int write_state;
    int latch_mode;
    int mode_int;

    int loadvalue;
    int value;

public:

    union {
        uint8_t flags;
        struct {
            bool armed:1;
            bool load:1;
            bool enabled:1;
            bool bcd:1;
        };
    };

    uint8_t write_lsb;
    uint8_t write_msb;
    uint8_t out;


public:
    CounterUnit();
    void reset();

    void SetMode(int new_mode, int new_latch_mode, int new_bcd_mode);
    void Latch(uint8_t w8);
    static void mode0_init(CounterUnit *ctx, int nclocks);
    static void mode0_count(CounterUnit *ctx, int nclocks);
    static void mode1_init(CounterUnit *ctx, int nclocks);
    static void mode1_count(CounterUnit *ctx, int nclocks);
    static void mode2_init(CounterUnit *ctx, int nclocks);
    static void mode2_count(CounterUnit *ctx, int nclocks);
    static void mode3_init(CounterUnit *ctx, int nclocks);
    static void mode3_count(CounterUnit *ctx, int nclocks);
    static void dummy_mode(CounterUnit *ctx, int nclocks);


    void (*counter_proc)(CounterUnit *, int);


    void count_clocks(int nclocks);
    void write_value(uint8_t w8);
    int read_value();

};


class I8253
{
public:
    uint32_t ccount_accu;   // benchmark counter

    CounterUnit counters[3];
    uint8_t control_word;
    //int clock_carry;
    int16_t counted_carry;
    int16_t accu_carry;

public:
    audio_sample_t * audio_buf; // pointer to external sound buf
    uint8_t beeper;
    uint8_t tapein;
    uint8_t covox;

    std::function<int(int)> count_tape;
public:
    I8253();
    void write_cw(uint8_t w8);
    void write(int addr, uint8_t w8);
    int read(int addr);
    void count_clocks(int nclocks);
    void gen_sound(int nclocks);
    void reset();
};

