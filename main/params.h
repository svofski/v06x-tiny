#pragma once

#define VERSION_STRING "0.1"

#define USE_BETTER_READDIR 1
#define OSD_USRUS_FRAMES_HOLD 25    // how many frames US+RUS is held before togglinig OSD

#define SCALER_CORE     1
#define EMU_CORE        0
#define AUDIO_CORE      1            // ideally should be a core separate from the emulator, but there's some sync problem
#define SDCARD_CORE     1

#define SCALER_PRIORITY (configMAX_PRIORITIES - 1)
#define EMU_PRIORITY    (configMAX_PRIORITIES - 1)
#define AUDIO_PRIORITY  (configMAX_PRIORITIES - 2)
#define SDCARD_PRIORITY (configMAX_PRIORITIES - 4)

#define AUDIO_NBUFFERS 2

//#define VI53_HIGH_FREQ_MUTE 1   // mute frequencies that are too high for our samplerate

#define WITH_I2S_AUDIO 1        // audio output via I2S to MAX98357A

#define AUDIO_SAMPLES_PER_FRAME (312*2)
#define AUDIO_SAMPLERATE        (AUDIO_SAMPLES_PER_FRAME * 50)
#define AUDIO_SAMPLE_SIZE       2 // int16_t

#define AUDIO_SCALE_8253 8     // shift 8253 by this many bits
#define AUDIO_SCALE_MASTER 4    // shift master sum by this many: 4 ok, 6 is loud but draws too much power

#define VI53_GENSOUND 1

#define I2S_DOUT      17
#define I2S_BCLK      0     // was 19 in some revisions of the board
#define I2S_LRC       18

#define PIN_NUM_CLK             12
#define PIN_NUM_MISO            13
#define PIN_NUM_MOSI            11
#define PIN_NUM_KEYBOARD_SS     19
#define PIN_NUM_SDCARD_SS       10

#define BOUNCE_NLINES 10 // bounce buffer height: 288 * 10/6 = 480: scale up 6 lines to 10

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Please update the following configuration according to your LCD spec //////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define LCD_PIXEL_CLOCK_HZ     20500000 // edging at 26500000+ 64+ fps // 20500000 50hz
#define LCD_BK_LIGHT_ON_LEVEL  1
#define LCD_BK_LIGHT_OFF_LEVEL !LCD_BK_LIGHT_ON_LEVEL
#define PIN_NUM_BK_LIGHT       2
#define PIN_NUM_HSYNC          39
#define PIN_NUM_VSYNC          41
#define PIN_NUM_DE             40
#define PIN_NUM_PCLK           42

#define PIN_NUM_DATA0          8  // B0
#define PIN_NUM_DATA1          3  // B1
#define PIN_NUM_DATA2          46 // B2
#define PIN_NUM_DATA3          9  // B3
#define PIN_NUM_DATA4          1  // B4

#define PIN_NUM_DATA5          5  // G0
#define PIN_NUM_DATA6          6  // G1
#define PIN_NUM_DATA7          7  // G2
#define PIN_NUM_DATA8          15 // G3
#define PIN_NUM_DATA9          16 // G4
#define PIN_NUM_DATA10         4  // G5

#define PIN_NUM_DATA11         45 // R0
#define PIN_NUM_DATA12         48 // R1
#define PIN_NUM_DATA13         47 // R2
#define PIN_NUM_DATA14         21 // R3
#define PIN_NUM_DATA15         14 // R4
#define PIN_NUM_DISP_EN        -1


// The pixel number in horizontal and vertical
#define LCD_H_RES              800
#define LCD_V_RES              480

#define BUFCOLUMNS 532

#define LCD_NUM_FB 0

#define SDCARD_FREQ_KHZ         20000
#define SDCARD_NRETRIES         10
extern const char * TAG;

typedef int16_t audio_sample_t;
