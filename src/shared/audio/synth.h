// Inspired by the Yamaha OPL-II and similar FM synthesizers.
// Supports additive and frequency modulation synthesis.
// Uses fixed-point arithmetic for audio processing to be fast on MCUs.

#pragma once

#include <stdint.h>
#include <stdlib.h>

#include <shared/utils/q1x15.h>

#include "buffer.h"

#define AUDIO_SYNTH_VOICE_COUNT 4
#define AUDIO_SYNTH_OPERATOR_COUNT 2
#define AUDIO_SYNTH_LUT_RES 10
#define AUDIO_SYNTH_LUT_SIZE (1 << AUDIO_SYNTH_LUT_RES)

typedef struct audio_synth_t audio_synth_t;
typedef struct audio_synth_voice_t audio_synth_voice_t;

typedef enum {
  // add operator to previous output
  AUDIO_SYNTH_OP_MODE_ADDITIVE,
  // use previous output as freq mod input
  AUDIO_SYNTH_OP_MODE_FREQ_MOD
} audio_synth_operator_mode_t;

typedef enum {
  AUDIO_SYNTH_OP_WAVEFORM_SINE,
} audio_synth_operator_waveform_t;

typedef struct audio_synth_operator_config_t {
  float freq_mult;                  // frequency multiplier
  audio_synth_operator_mode_t mode; // operator mode
  q1x15 level;                      // output level
} audio_synth_operator_config_t;

static const audio_synth_operator_config_t audio_synth_operator_config_default =
    {.freq_mult = 1.0f,
     .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
     .level = Q1X15_ZERO};

typedef struct audio_synth_operator_t {
  audio_synth_operator_config_t config;

  // state
  uint32_t phase;   // wave phase
  uint32_t d_phase; // wave increment (derived from freq and mult)

  // todo: feedback
  // todo: adsr

  audio_synth_voice_t *voice;
} audio_synth_operator_t;

typedef struct audio_synth_voice_t {
  audio_synth_operator_t ops[AUDIO_SYNTH_OPERATOR_COUNT];
  audio_synth_t *synth;
} audio_synth_voice_t;

typedef struct audio_synth_t {
  float sample_rate;
  q1x15 master_level;
  audio_synth_voice_t voices[AUDIO_SYNTH_VOICE_COUNT];
} audio_synth_t;

void audio_synth_init(audio_synth_t *synth, float sample_rate);

void audio_synth_operator_fill_buffer(audio_synth_operator_t *op, q1x15 *buffer,
                                      uint32_t buffer_size);

void audio_synth_voice_set_freq(audio_synth_voice_t *voice, float frequency);
void audio_synth_voice_fill_buffer(audio_synth_voice_t *voice, q1x15 *buffer,
                                   uint32_t buffer_size);

void audio_synth_fill_buffer(audio_synth_t *synth, audio_buffer_t buffer,
                             uint32_t buffer_size);
