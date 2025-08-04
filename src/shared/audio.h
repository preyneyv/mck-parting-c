#pragma once

#include "audio/buffer.h"
#include "audio/synth.h"

typedef struct {
  audio_buffer_pool_t pool;
  audio_synth_t synth;
  bool enabled;
} audio_t;

void audio_init(audio_t *audio);
void audio_set_enabled(audio_t *audio, bool enabled);
