#pragma once

#include <stdint.h>

#define PROJECT_NAME "Prythm DX"

//// SYSTEM CONFIGURATION ////
#define TARGET_FPS 240
static const uint32_t TARGET_FRAME_INTERVAL_US = 1000000 / TARGET_FPS;

//// DISPLAY CONFIGURATION ////
#define DISP_WIDTH 128
#define DISP_HEIGHT 64
static const uint32_t DISP_PIX = DISP_WIDTH * DISP_HEIGHT;

//// AUDIO CONFIGURATION ////
#define AUDIO_BIT_DEPTH 16
#define AUDIO_SAMPLE_RATE 48000
