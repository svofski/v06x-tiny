#pragma once

namespace scaler
{

extern volatile int fps;
extern volatile int v06x_fps;
extern volatile uint64_t frameduration_us;

// updated by esp_filler
extern volatile int v06x_framecount;
extern volatile int v06x_frame_cycles;

extern uint8_t * bounce_buf8[2];

void allocate_buffers();
void create_pinned_to_core();
void main_screen_turn_on();

}