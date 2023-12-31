#pragma once

#include <cstdint>
#include <functional>

#include "esp_attr.h"

class CounterUnit
{
    friend class TestOfCounterUnit;

    int latch_value;
    int write_state;
    int latch_mode;
    int mode_int;

    uint8_t write_lsb;
    uint8_t write_msb;



public:
    union {
        uint32_t flags;
        struct {
            bool armed:1;
            bool load:1;
            bool enabled:1;
            bool bcd:1;
        };
    };

    int out;
    std::function<void(void)> on_out_changed;
    uint16_t loadvalue;
    int value;

public:
    CounterUnit();
    void reset();

    void set_out(int value) {        
        this->out = value;
        on_out_changed();
    }

    void SetMode(int new_mode, int new_latch_mode, int new_bcd_mode);
    void Latch(uint8_t w8);
    void mode0(int nclocks);
    void mode1(int nclocks);
    void mode2(int nclocks);
    void mode3(int nclocks);
    void mode4(int nclocks);
    void mode5(int nclocks);

    void count_clocks(int nclocks);
    void write_value(uint8_t w8);
    int read_value();
};


class I8253
{
private:
    CounterUnit counters[3];
    uint8_t control_word;
    int sum;

public:
    I8253();
    void out_changed();    
    void write_cw(uint8_t w8);
    void write(int addr, uint8_t w8);
    int read(int addr);
    void count_clocks(int nclocks);
    void reset();

    int out_sum() const { return sum; }
};

