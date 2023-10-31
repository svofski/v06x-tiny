#pragma once

#define SCALER_CORE 1
#define EMU_CORE 0
#define AUDIO_CORE 0            // ideally should be a core separate from the emulator, but there's some sync problem
#define AUDIO_NBUFFERS 4

#define VI53_HIGH_FREQ_MUTE 1   // mute frequencies that are too high for our samplerate

#define WITH_I2S_AUDIO 1
//#define WITH_PWM_AUDIO

#define AUDIO_SAMPLES_PER_FRAME (312*2)
#define AUDIO_SAMPLERATE        (AUDIO_SAMPLES_PER_FRAME * 50)
#define AUDIO_SAMPLE_SIZE       2 // int16_t


#define I2S_DOUT      17
#define I2S_BCLK      0     // was 19 in some revisions of the board
#define I2S_LRC       18
