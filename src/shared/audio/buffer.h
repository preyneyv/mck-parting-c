#pragma once

#include <stdint.h>

typedef uint32_t *audio_buffer_t;

static inline uint32_t audio_buffer_frame_from_stereo(int16_t left,
                                                      int16_t right)
{
  uint16_t left_u = (uint16_t)left;
  uint16_t right_u = (uint16_t)right;
  return ((uint32_t)left_u << 16) | (uint32_t)right_u;
}

static inline uint32_t audio_buffer_frame_from_mono(int16_t mono_sample)
{
  return audio_buffer_frame_from_stereo(mono_sample, mono_sample);
}
