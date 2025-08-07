#pragma once

#include <stdint.h>

#define PROJECT_NAME "prism"

//// SYSTEM CONFIGURATION ////
#define TARGET_FPS 120
static const uint32_t TARGET_FRAME_INTERVAL_US = 1000000 / TARGET_FPS;
#define TICK_RATE 1000 // 1000 ticks per second (tune if needed)
static const uint32_t TICK_INTERVAL_US = 1000000 / TICK_RATE;

//// DISPLAY CONFIGURATION ////
#define DISP_WIDTH 128
#define DISP_HEIGHT 64
static const uint32_t DISP_PIX = DISP_WIDTH * DISP_HEIGHT;

//// AUDIO CONFIGURATION ////
// changing this will require modifications on datatypes and bit shifts for
// audio buffers.
#define AUDIO_BIT_DEPTH 16
// changing the sample rate will change the sound because of FM freq. while
// fixable, it costs more float ops in the audio hot loop and is largely
// unnecessary.
#define AUDIO_SAMPLE_RATE 48000
#define AUDIO_SYNTH_TIMEBASE 1000 // 1 second
