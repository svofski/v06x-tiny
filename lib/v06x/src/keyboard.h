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
constexpr uint8_t PC_BIT_SS = (1<<5);
constexpr uint8_t PC_BIT_US = (1<<6);
constexpr uint8_t PC_BIT_RUSLAT = (1<<7);
constexpr uint8_t PC_MDOKEYS_MASK = (PC_BIT_SS | PC_BIT_US | PC_BIT_RUSLAT);

struct keyboard_state_t
{
    uint8_t rows;
    uint8_t pc;
};

extern std::function<void(bool)> onreset;

extern keyboard_state_t state;

void select_columns(uint8_t pa, uint8_t pc); // out 03 (PA) and forget
void update_state();  // finalize SPI transaction and update state

void init(void);

}
