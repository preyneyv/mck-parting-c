#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t *audio_buffer_t;

typedef struct {
  // each sample is ((LEFT << 16) | RIGHT) stereo, S16 per channel
  // to match MAX98357A's S16 stereo format.
  audio_buffer_t buffers;

  uint8_t size;         // total number of buffers
  uint32_t buffer_size; // number of samples per buffer

  volatile uint8_t count; // number of filled buffers

  uint8_t write_head;
  uint8_t read_head;
} audio_buffer_pool_t;

void audio_buffer_pool_init(audio_buffer_pool_t *pool, uint8_t size,
                            uint32_t buffer_size);
void audio_buffer_pool_free(audio_buffer_pool_t *pool);
uint32_t *audio_buffer_pool_acquire_write(audio_buffer_pool_t *pool,
                                          bool blocking);
void audio_buffer_pool_commit_write(audio_buffer_pool_t *pool);
uint32_t *audio_buffer_pool_acquire_read(audio_buffer_pool_t *pool,
                                         bool blocking);
void audio_buffer_pool_commit_read(audio_buffer_pool_t *pool);

static inline uint32_t audio_buffer_frame_from_stereo(int16_t left,
                                                      int16_t right) {
  uint16_t left_u = (uint16_t)left;
  uint16_t right_u = (uint16_t)right;
  return ((uint32_t)left_u << 16) | (uint32_t)right_u;
}

static inline uint32_t audio_buffer_frame_from_mono(int16_t mono_sample) {
  return audio_buffer_frame_from_stereo(mono_sample, mono_sample);
}
