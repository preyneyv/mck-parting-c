#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <shared/utils/q1x15.h>

#include "buffer.h"

#define AUDIO_SYNTH_MAX_VOICES 4

typedef struct audio_synth_t audio_synth_t;

typedef struct audio_synth_operator_t {
  float freq_mult;

  uint32_t phase;
  uint32_t d_phase;

  q1x15 level;

  // todo: feedback
  // todo: adsr
} audio_synth_operator_t;

typedef struct audio_synth_voice_t {
  // todo: audio matrix?? would be cool but maybe overkill?

  bool is_fm; // false = [op1, op2] ; true = [op1 -> op2]
  audio_synth_operator_t op1;
  audio_synth_operator_t op2;

  audio_synth_t *synth;
} audio_synth_voice_t;

typedef struct audio_synth_t {
  float sample_rate;
  audio_synth_voice_t voices[AUDIO_SYNTH_MAX_VOICES];
} audio_synth_t;

void audio_synth_init(audio_synth_t *synth, float sample_rate);

void audio_synth_operator_fill_buffer(audio_synth_operator_t *op, q1x15 *buffer,
                                      uint32_t buffer_size);

void audio_synth_voice_set_freq(audio_synth_voice_t *voice, float frequency);
void audio_synth_voice_fill_buffer(audio_synth_voice_t *voice, q1x15 *buffer,
                                   uint32_t buffer_size);

void audio_synth_fill_buffer(audio_synth_t *synth, audio_buffer_t buffer,
                             uint32_t buffer_size);
