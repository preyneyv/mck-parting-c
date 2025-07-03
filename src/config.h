#ifndef CONFIG_H
#define CONFIG_H

//// SYSTEM CONFIGURATION ////
/** System clock in KHz */
#define SYS_CLOCK_KHZ 176000





//// AUDIO CONFIGURATION ////
/** GPIO used for audio output */
#define AUDIO_DAC_PIN 26

/** DAC sample rate in Hz */
#define AUDIO_DAC_SAMPLE_RATE 22000

#if SYS_CLOCK_KHZ % AUDIO_DAC_SAMPLE_RATE != 0
#error "AUDIO_DAC_SAMPLE_RATE is not a divisor of SYS_CLOCK_KHZ"
#endif

#endif // CONFIG_H
