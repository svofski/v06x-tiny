#pragma once

#include <cstdint>
#include <functional>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#include "params.h"

namespace keyboard
{

// mod keys are active low
constexpr uint8_t BLK_BIT_VVOD =     (1 << 0);
constexpr uint8_t BLK_BIT_SBROS =    (1 << 1);
constexpr uint8_t BLK_MASK = BLK_BIT_VVOD | BLK_BIT_SBROS;

constexpr uint8_t PC_BIT_SS =       (1<<5);
constexpr uint8_t PC_BIT_US =       (1<<6);
constexpr uint8_t PC_BIT_RUSLAT =   (1<<7);
// only the modkeys that go to port C
constexpr uint8_t PC_MODKEYS_MASK = PC_BIT_SS | PC_BIT_US | PC_BIT_RUSLAT;

struct keyboard_state_t
{
    uint8_t rows;           // rows input
    uint8_t pc;             // modkeys input
    uint8_t blk;            // vvod/sbros
    uint8_t ruslat;         // output 
};

extern keyboard_state_t state;

void select_columns(uint8_t pa); // out 03 (PA) and forget
void read_rows();  // finalize SPI transaction and update state
void read_modkeys();
void out_ruslat(uint8_t w8);
void commit_ruslat();
void init(void);

bool vvod_pressed();
bool sbros_pressed();

}
