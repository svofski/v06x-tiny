#include <cstdint>
#include <functional>
#include <esp_attr.h>
#include "8253.h"
#include <stdio.h>

#include "../../../main/params.h"

#pragma GCC optimize("O3,jump-tables") // unroll-loops probably isn't helping here

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

static inline uint32_t getccount()
{
    uint32_t ccount;
    asm volatile("rsr.ccount %0" : "=a"(ccount));
    return ccount;
}

IRAM_ATTR
CounterUnit::CounterUnit()
{
    this->reset();
}

IRAM_ATTR
void CounterUnit::reset()
{
    latch_value = -1;
    write_state = 0;
    latch_mode = 0;
    mode_int = 0;
    loadvalue = 0;
    flags = 0;
    value = 0;
    counter_proc = dummy_mode;
}

IRAM_ATTR
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
            this->out = 0;
            this->armed = true;
            this->enabled = false;
            this->counter_proc = mode0_init;
            break;
        case 1:
            this->out = 1;
            this->armed = true;
            this->enabled = false;
            this->counter_proc = mode1_init;
            break;
        case 2:
            this->out = 1;
            this->enabled = false;
            this->counter_proc = mode2_init;
            // armed?
            break;
        case 3:
            this->out = 1;
            this->enabled = false;
            this->counter_proc = mode3_init;  // expect reload value
            // armed?
            break;
        default:
            this->out = 1;
            this->enabled = false;
            this->counter_proc = dummy_mode;
            // armed?
    }
    this->load = false;
    this->latch_mode = new_latch_mode;
    this->write_state = 0;
}

IRAM_ATTR
void CounterUnit::Latch(uint8_t w8) {
    this->count_clocks(1);
    this->latch_value = this->value;
}

IRAM_ATTR
void CounterUnit::mode0_init(CounterUnit *ctx, int nclocks) // Interrupt on terminal count
{
    if (ctx->load) {
        ctx->value = ctx->loadvalue;
        ctx->enabled = true;
        ctx->armed = true;
        ctx->out = 0;
        ctx->load = false;
        ctx->counter_proc = mode0_count;
        mode0_count(ctx, nclocks);
    }
}

IRAM_ATTR
void CounterUnit::mode0_count(CounterUnit *ctx, int nclocks)
{
   int previous = ctx->value;
   ctx->value -= nclocks;
   if (ctx->value <= 0) {
       if (ctx->armed) {
           if (previous != 0) ctx->out = 1;
           ctx->armed = false;
       }
       ctx->value += ctx->bcd ? 10000 : 65536;
   }
}

IRAM_ATTR
void CounterUnit::mode1_init(CounterUnit *ctx, int nclocks)
{
    // Programmable one-shot
    if (ctx->load) {
        //ctx->value = ctx->loadvalue;  -- quirk!
        ctx->enabled = true;
        ctx->counter_proc = mode1_count;
        mode1_count(ctx, nclocks);
    }
}

IRAM_ATTR
void CounterUnit::mode1_count(CounterUnit *ctx, int nclocks)
{
    ctx->value -= nclocks;
    if (ctx->value <= 0) {
        ctx->value += ctx->loadvalue;
    }
}

IRAM_ATTR
void CounterUnit::mode2_init(CounterUnit *ctx, int nclocks)
{
    if (ctx->load) {
        ctx->value = ctx->loadvalue;
        ctx->load = false;
        ctx->enabled = true;
        ctx->counter_proc = mode2_count;
        mode2_count(ctx, nclocks);
    }
}

IRAM_ATTR
void CounterUnit::mode2_count(CounterUnit *ctx, int nclocks)
{
    ctx->value -= nclocks;
    if (ctx->value <= 0) {
        int skips = (-ctx->value) / ctx->loadvalue + 1;
        ctx->value += skips * ctx->loadvalue;
    }
}

IRAM_ATTR
void CounterUnit::mode3_init(CounterUnit *ctx, int nclocks)
{
    if (ctx->load) {
        ctx->value = ctx->loadvalue;
        ctx->load = false;
        ctx->counter_proc = mode3_count;
        mode3_count(ctx, nclocks);
    }
}

IRAM_ATTR
void CounterUnit::mode3_count(CounterUnit *ctx, int nclocks)
{
    ctx->value -= nclocks + nclocks;
    while (ctx->value <= 0) {
        ctx->out ^= 1;
        int reload = ctx->loadvalue;
        ctx->value += reload;
        if ((reload & 1) == 1) {
            ctx->value -= ctx->out == 0 ? 3 : 1;
        }
    }
}

//IRAM_ATTR
void CounterUnit::dummy_mode(CounterUnit *ctx, int nclocks)
{
}

IRAM_ATTR
inline void CounterUnit::count_clocks(int nclocks)
{
    this->counter_proc(this, nclocks);
}

IRAM_ATTR
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
        // adjust reload value for mode 3
        if (this->mode_int == 1) {
            this->loadvalue = (this->loadvalue == 0) ? (this->bcd ? 10000 : 0x10000 ) : (this->loadvalue + 1);
        }
        else if (this->mode_int == 2) {
            this->loadvalue = (this->loadvalue == 0) ? (this->bcd ? 10000 : 0x10000) : this->loadvalue;
        }
        else if (this->mode_int == 3 && this->loadvalue == 0) {
            this->loadvalue = this->bcd ? 10000 : 0x10000;
        }
    }
}

IRAM_ATTR
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

IRAM_ATTR
I8253::I8253() : control_word(0)
{
    this->counted_carry = 0;
    this->accu_carry = 0;
    this->beeper = 0;
    this->covox = 0;
}

IRAM_ATTR
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

IRAM_ATTR
void I8253::write(int addr, uint8_t w8)
{
    switch (addr & 3) {
        case 0x03:
            return this->write_cw(w8);
        default:
            return this->counters[addr & 3].write_value(w8);
    }
}

IRAM_ATTR
int I8253::read(int addr)
{
    switch (addr & 3) {
        case 0x03:
            return this->control_word;
        default:
            return this->counters[addr & 3].read_value();
    }
}

IRAM_ATTR
void I8253::count_clocks(int nclocks)
{
    this->counters[0].count_clocks(nclocks);
    this->counters[1].count_clocks(nclocks);
    this->counters[2].count_clocks(nclocks);
}

IRAM_ATTR
void I8253::reset()
{
    this->counters[0].reset();
    this->counters[1].reset();
    this->counters[2].reset();
    this->counted_carry = 0;
    this->accu_carry = 0;
}

IRAM_ATTR
void I8253::gen_sound(int nclocks)
{
    uint32_t ccount = getccount();

    //printf("gen_sound: %d\n", nclocks);
    constexpr int16_t mul = 12; //48;              // 6 == good sound but begins flickering in bolderm when diagonal scrolling
    constexpr int16_t div = VI53_CLOCKS_PER_SAMPLE / mul;
    int16_t remaining = nclocks;
    int16_t counted = this->counted_carry;
    int16_t accu = this->accu_carry;
    int16_t count_rem = 0;
    int16_t count = 0;
    if (counted) {
        // not full clocks count remainder from the previous call
        count_rem = counted % mul;                                      // how much of the last "mul" was counted
        count = std::min((int16_t)(mul - count_rem), remaining);        // how much to add to make a full "mul"
    }

    if (!count) count = std::min(mul, remaining);   // if there's no remainder, count as much as possible

    for (; remaining > 0; ) {
        //printf("  count=%d remaining=%d\n", count, remaining);
        count_clocks(count);
        counted += count;
        remaining -= count;
        if (count_tape != nullptr) {
            tapein = count_tape(count << 1);
        }
        if (count + count_rem == mul) {
            accu += counters[0].out + counters[1].out + counters[2].out + beeper + tapein;
            // test: crackles
            //accu += 3;
            // add tape out here i guess?
        }
        count_rem = 0;  // forget remainder
        if (counted >= VI53_CLOCKS_PER_SAMPLE) {
            *audio_buf++ = (accu << AUDIO_SCALE_8253) / div;
            accu = 0;
            counted -= VI53_CLOCKS_PER_SAMPLE;
        }

        count = std::min(mul, remaining);
    }

    // carry over to the next call
    this->counted_carry = counted;
    this->accu_carry = accu;

    this->ccount_accu += getccount() - ccount;
}
