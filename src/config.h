#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

//// SYSTEM CONFIGURATION ////
/** System clock in KHz */
#define SYS_CLOCK_KHZ 176000
#define TARGET_FPS 10000

//// AUDIO CONFIGURATION ////
/** GPIO used for audio output */
#define AUDIO_DAC_PIN 26

/** DAC sample rate in Hz */
#define AUDIO_DAC_SAMPLE_RATE 22000

#if SYS_CLOCK_KHZ % AUDIO_DAC_SAMPLE_RATE != 0
#error "AUDIO_DAC_SAMPLE_RATE is not a divisor of SYS_CLOCK_KHZ"
#endif

static const uint32_t TARGET_FRAME_INTERVAL_US = 1000000 / TARGET_FPS;
#endif // CONFIG_H
