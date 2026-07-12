#pragma once

#include <stdint.h>

#include <prism/graphics/display.h>
#include <prism/types.h>

#define PROJECT_NAME "prism"

#ifndef PRISM_ENABLE_PERFORMANCE_LOGS
#define PRISM_ENABLE_PERFORMANCE_LOGS 0
#endif

//// SYSTEM CONFIGURATION ////
#define TARGET_FPS 120
static const uint32_t TARGET_FRAME_INTERVAL_US = 1000000 / TARGET_FPS;
_Static_assert(PRISM_ENGINE_TICK_RATE % TARGET_FPS == 0,
               "engine tick rate must be divisible by the display rate");

//// DISPLAY CONFIGURATION ////
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

//// POWER MANAGEMENT ////
// Auto-sleep after this many milliseconds of no input.
#define AUTO_SLEEP_TIMEOUT_MS 30000
// Smoothly fade display and LED output during the final idle interval.
#define AUTO_SLEEP_FADE_MS 3000

/* Flash-safe multicore lockout may legitimately wait up to one second for the
 * audio core. Storage loops feed the watchdog per sector/page, so this window
 * catches real hangs without resetting during a valid lockout handshake. */
#define WATCHDOG_TIMEOUT_MS 2500
