#pragma once

#include <cstdint>
#include "params.h"

namespace audio
{
extern audio_sample_t * audio_pp[AUDIO_NBUFFERS];
extern uint8_t * ay_pp[AUDIO_NBUFFERS];

void allocate_buffers(void);
void create_pinned_to_core(void);

}