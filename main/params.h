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


#define CONFIG_EXAMPLE_USE_BOUNCE_BUFFER 1
#define CONFIG_BOUNCE_ONLY 1
#define BOUNCE_NLINES 10 // 288 * 10/6: scale up 6 lines to 10
//#define SCALE85 0

#define FULLBUFFER 0        // use full-screen buffer
#define DOUBLE_FB 1         // double-buffered buffer


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_PIXEL_CLOCK_HZ     20500000 // edging at 26500000+ 64+ fps // 20500000 50hz
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT       2
#define EXAMPLE_PIN_NUM_HSYNC          39
#define EXAMPLE_PIN_NUM_VSYNC          41
#define EXAMPLE_PIN_NUM_DE             40
#define EXAMPLE_PIN_NUM_PCLK           42

#define EXAMPLE_PIN_NUM_DATA0          8  // B0
#define EXAMPLE_PIN_NUM_DATA1          3  // B1
#define EXAMPLE_PIN_NUM_DATA2          46 // B2
#define EXAMPLE_PIN_NUM_DATA3          9  // B3
#define EXAMPLE_PIN_NUM_DATA4          1  // B4

#define EXAMPLE_PIN_NUM_DATA5          5  // G0
#define EXAMPLE_PIN_NUM_DATA6          6  // G1
#define EXAMPLE_PIN_NUM_DATA7          7  // G2
#define EXAMPLE_PIN_NUM_DATA8          15 // G3
#define EXAMPLE_PIN_NUM_DATA9          16 // G4
#define EXAMPLE_PIN_NUM_DATA10         4  // G5

#define EXAMPLE_PIN_NUM_DATA11         45 // R0
#define EXAMPLE_PIN_NUM_DATA12         48 // R1
#define EXAMPLE_PIN_NUM_DATA13         47 // R2
#define EXAMPLE_PIN_NUM_DATA14         21 // R3
#define EXAMPLE_PIN_NUM_DATA15         14 // R4
#define EXAMPLE_PIN_NUM_DISP_EN        -1


// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES              800
#define EXAMPLE_LCD_V_RES              480

#define BUFCOLUMNS 532


#if FULLBUFFER
#if DOUBLE_FB
#define LCD_NUM_FB             2
#else
#define LCD_NUM_FB             1
#endif
#else
#define LCD_NUM_FB 0
#endif

extern const char * TAG;

typedef int16_t audio_sample_t;
