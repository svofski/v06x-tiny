#pragma once

#include "osd.h"

namespace scaler
{

extern volatile int fps;
extern volatile int v06x_fps;
extern volatile uint64_t frameduration_us;

extern uint8_t * bounce_buf8[2];

void allocate_buffers();
void create_pinned_to_core();
void main_screen_turn_on();

void show_osd(OSD * osd);

}