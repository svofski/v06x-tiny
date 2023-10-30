#include <cstdint>
#include <functional>
#include <esp_attr.h>
#include "8253.h"
#include <stdio.h>
static uint16_t tobcd(uint16_t x) {
    int result = 0;
    for (int i = 0; i < 4; ++i) {
        result |= (x % 10) << (i * 4);
        x /= 10;
    }
    return result;
}

static uint16_t frombcd(uint16_t x) {
    int result = 0;
    for (int i = 0; i < 4; ++i) {
        int digit = (x & 0xf000) >> 12;
        if (digit > 9) digit = 9;
        result = result * 10 + digit;
        x <<= 4;
    }
    return result;
}

CounterUnit::CounterUnit()
{
    this->reset();
}

void CounterUnit::reset()
{
    latch_value = -1;
    write_state = 0;
    latch_mode = 0;
    mode_int = 0;
    loadvalue = 0;
    flags = 0;    
    value = 0;
}

void CounterUnit::SetMode(int new_mode, int new_latch_mode, int new_bcd_mode) 
{
    this->count_clocks(1);
    this->bcd = new_bcd_mode;
    if ((new_mode & 0x03) == 2) {
        this->mode_int = 2;
    } else if ((new_mode & 0x03) == 3) {
        this->mode_int = 3;
    } else {
        this->mode_int = new_mode;
    }

    switch(this->mode_int) {
        case 0:
            this->set_out(0);
            this->armed = true;
            this->enabled = false;
            break;
        case 1:
            this->set_out(1);
            this->armed = true;
            this->enabled = false;
            break;
        case 2:
            this->set_out(1);
            this->enabled = false;
            // armed?
            break;
        case 3:
            this->set_out(1);
            this->enabled = false;
            // armed?
            break;
        default:
            this->set_out(1);
            this->enabled = false;
            // armed?
    }
    this->load = false;
    this->latch_mode = new_latch_mode;
    this->write_state = 0;
}

void CounterUnit::Latch(uint8_t w8) {
    this->count_clocks(1);
    this->latch_value = this->value;
}

IRAM_ATTR
void CounterUnit::mode0(int nclocks) // Interrupt on terminal count
{    
    if (this->load) {
        this->value = this->loadvalue;
        this->enabled = true;
        this->armed = true;
        this->set_out(0);
        this->load = false;
    }
    if (this->enabled) {
        int previous = this->value;
        this->value -= nclocks;
        if (this->value <= 0) {
            if (this->armed) {
                if (previous != 0) this->set_out(1);
                this->armed = false;
            }
            this->value += this->bcd ? 10000 : 65536;
        }
    }
}

IRAM_ATTR
void CounterUnit::mode1(int nclocks)
{        
    // Programmable one-shot
    if (!this->enabled && this->load) {
        this->enabled = true;
    }

    if (this->enabled) {
        this->value -= nclocks;
        if (this->value <= 0) {
            int reload = this->loadvalue == 0 ? 
                (this->bcd ? 10000 : 0x10000 ) : (this->loadvalue + 1);
            this->value += reload;
        }
    }
}

IRAM_ATTR
void CounterUnit::mode2(int nclocks)
{    
    // Rate generator
    if (!this->enabled && this->load) {
        this->value = this->loadvalue;
        this->load = false;
        this->enabled = true;
    }
    this->value -= nclocks;
    if (this->value <= 0) {
        do {
            int reload = this->loadvalue == 0 ? (this->bcd ? 10000 : 0x10000 ) : this->loadvalue;
            this->value += reload;
        } while(this->value <= 0);
    }
}

IRAM_ATTR
inline void CounterUnit::mode3(int nclocks)
{
    // Square wave generator
    if (!this->enabled && this->load) {
        this->value = this->loadvalue;
        this->load = false;
        this->enabled = true;
    }
    if (this->enabled) {
        this->value -= nclocks + nclocks;
        if (this->value <= 0) {
            do {
                this->set_out(this->out ^= 1);
                int reload = (this->loadvalue == 0) ? (this->bcd ? 10000 : 0x10000) : this->loadvalue;
                this->value += reload;
                if ((reload & 1) == 1) {
                    this->value -= this->out == 0 ? 3 : 1;
                }
                //printf("this->value=%d\n", this->value);
            } while(this->value <= 0);
        }
    }
}

void CounterUnit::mode4(int nclocks)
{        
}
void CounterUnit::mode5(int nclocks)
{        
}

IRAM_ATTR
void CounterUnit::count_clocks(int nclocks) 
{
    switch (this->mode_int) {
        case 0: // Interrupt on terminal count
            mode0(nclocks);
            break;
        case 1: // Programmable one-shot
            mode1(nclocks);
            break;
        case 2: // Rate generator
            mode2(nclocks);
            break;
        case 3: // Square wave generator
            mode3(nclocks);
            break;
        case 4: // Software triggered strobe
            break;
        case 5: // Hardware triggered strobe
            break;
        default:
            break;
    }
}


void CounterUnit::write_value(uint8_t w8) {
    if (this->latch_mode == 3) {
        // lsb, msb             
        switch (this->write_state) {
            case 0:
                this->write_lsb = w8;
                this->write_state = 1;
                break;
            case 1:
                this->write_msb = w8;
                this->write_state = 0;
                this->loadvalue = ((this->write_msb << 8) & 0xffff) | 
                    (this->write_lsb & 0xff);
                this->load = true;
                //printf("load_value=%d\n", this->loadvalue);
                break;
            default:
                break;
        }
    } else if (this->latch_mode == 1) {
        // lsb only
        this->loadvalue = w8;
        this->load = true;
    } else if (this->latch_mode == 2) {
        // msb only 
        this->value = w8 << 8;
        this->value &= 0xffff;
        this->loadvalue = this->value;
        this->load = true;
    }
    if (this->load) {
        if (this->bcd) {
            this->loadvalue = frombcd(this->loadvalue);
        }
    }
}

int CounterUnit::read_value()
{
    int value = 0;
    switch (this->latch_mode) {
        case 0:
            // impossibru
            break;
        case 1:
            value = this->latch_value != -1 ? this->latch_value : this->value;
            this->latch_value = -1; 
            value = this->bcd ? tobcd(value) : value;
            value &= 0xff;
            break;
        case 2:
            value = this->latch_value != -1 ? this->latch_value : this->value;
            this->latch_value = -1; 
            value = this->bcd ? tobcd(value) : value;
            value = (value >> 8) & 0xff;
            break;
        case 3:
            value = this->latch_value != -1 ? this->latch_value : this->value;
            value = this->bcd ? tobcd(value) : value;
            switch(this->write_state) {
                case 0:
                    this->write_state = 1;
                    value = value & 0xff;
                    break;
                case 1:
                    this->latch_value = -1;
                    this->write_state = 0;
                    value = (value >> 8) & 0xff;
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    return value;
}



I8253::I8253() : control_word(0) 
{
    for (int ctr = 0; ctr < 3; ++ctr) {
        counters[ctr].on_out_changed = std::bind(&I8253::out_changed, this);
    }
}

void I8253::out_changed()
{
    sum = counters[0].out + counters[1].out + counters[2].out;
}

void I8253::write_cw(uint8_t w8) 
{
    unsigned counter_set = (w8 >> 6) & 3;
    int mode_set = (w8 >> 1) & 3;
    int latch_set = (w8 >> 4) & 3;
    int bcd_set = (w8 & 1);

    if ((unsigned)counter_set >= sizeof(counters)/sizeof(counters[0])) {
        // error
        return;
    }

    CounterUnit & ctr = this->counters[counter_set];
    if (latch_set == 0) {
        ctr.Latch(latch_set);
    } else {
        ctr.SetMode(mode_set, latch_set, bcd_set);
    }
}

void I8253::write(int addr, uint8_t w8) 
{
    switch (addr & 3) {
        case 0x03:
            return this->write_cw(w8);
        default:
            return this->counters[addr & 3].write_value(w8);
    }
}

int I8253::read(int addr)
{
    switch (addr & 3) {
        case 0x03:
            return this->control_word;
        default:
            return this->counters[addr & 3].read_value();
    }
}

void I8253::count_clocks(int nclocks)
{
    this->counters[0].count_clocks(nclocks);
    this->counters[1].count_clocks(nclocks);
    this->counters[2].count_clocks(nclocks);
}

void I8253::reset()
{
    this->counters[0].reset();
    this->counters[1].reset();
    this->counters[2].reset();
}
