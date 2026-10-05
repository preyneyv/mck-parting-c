#pragma once

#include <stdint.h>

#include <prism/graphics/display.h>
#include <prism/types.h>

#define PROJECT_NAME "prism"

#ifndef PRISM_ENABLE_PERFORMANCE_LOGS
#define PRISM_ENABLE_PERFORMANCE_LOGS 0
#endif

#define TARGET_FPS 120
static const uint32_t TARGET_FRAME_INTERVAL_US = 1000000 / TARGET_FPS;
_Static_assert(PRISM_ENGINE_TICK_RATE % TARGET_FPS == 0,
               "engine tick rate must be divisible by the display rate");

// The audio buffer types and shifts assume 16-bit samples.
#define AUDIO_BIT_DEPTH 16
// FM patches are tuned for this rate; changing it changes their sound.
#define AUDIO_SAMPLE_RATE 48000
// Synth envelope times are in milliseconds.
#define AUDIO_SYNTH_TIMEBASE 1000

// Auto-sleep after this long without input, fading the display and LEDs out
// over the final AUTO_SLEEP_FADE_MS.
#define AUTO_SLEEP_TIMEOUT_MS 30000
#define AUTO_SLEEP_FADE_MS 3000

/* A flash-safe multicore lockout can wait up to one second for the audio core.
 * Storage loops feed the watchdog per sector/page, so this catches real hangs
 * without firing during a valid lockout. */
#define WATCHDOG_TIMEOUT_MS 2500
