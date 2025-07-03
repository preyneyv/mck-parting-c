#ifndef AUDIO_TEST_H
#define AUDIO_TEST_H

#include <stdint.h>
#include <stdbool.h>

// Audio buffer configuration
#define BUFFER_SIZE 256
#define HALF_BUFFER_SIZE (BUFFER_SIZE / 2)

// External audio buffer that synthesis code can write to
extern uint8_t audio_buffer[BUFFER_SIZE];
extern volatile int buffer_position;
extern volatile bool need_fill_first_half;
extern volatile bool need_fill_second_half;

// Audio system functions
int audio_init(uint32_t sample_rate_hz);
int audio_start(void);
void audio_stop(void);
void audio_cleanup(void);

// Synthesis function - implement this with your synthesis code
void synth_fill_buffers(void);

#endif // AUDIO_TEST_H
