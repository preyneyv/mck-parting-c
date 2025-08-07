// HAL for audio subsystem

#pragma once

#include "audio/buffer.h"
#include "audio/synth.h"

// todo: way to enter powersaving?
typedef struct {
  audio_buffer_pool_t pool;
  audio_synth_t synth;
} audio_t;

void audio_init(audio_t *audio);
