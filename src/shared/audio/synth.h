// Inspired by the Yamaha OPL-II and similar FM synthesizers.
// Supports additive and frequency modulation synthesis.
// Uses fixed-point arithmetic for audio processing to be fast on MCUs.

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

typedef struct audio_synth_env_config_stage_t {
  uint32_t duration; // duration in timebase
  q1x15 level;       // target level
  // env curvature?
} audio_synth_env_config_stage_t;

typedef struct audio_synth_env_config_t {
  audio_synth_env_config_stage_t a;
  audio_synth_env_config_stage_t d;
  audio_synth_env_config_stage_t s;
  audio_synth_env_config_stage_t r;
} audio_synth_env_config_t;

typedef struct audio_synth_env_state_stage_t {
  uint32_t duration; // sample count in this stage
  q1x15 d_level;     // change per sample in this stage
  q1x15 level;       // target level for this stage
} audio_synth_env_state_stage_t;

typedef struct audio_synth_env_state_t {
  q1x15 level;        // current level
  uint32_t evolution; // evolution in sample count
  uint8_t stage;      // current stage (0 = A, 1 = D, 2 = S, 3 = R)

} audio_synth_env_state_t;

typedef struct audio_synth_operator_config_t {
  float freq_mult;                  // frequency multiplier
  q1x15 level;                      // output level
  audio_synth_operator_mode_t mode; // operator mode
  audio_synth_env_config_t env;     // envelope config
  // todo: waveform
} audio_synth_operator_config_t;

static const audio_synth_operator_config_t audio_synth_operator_config_default =
    {.freq_mult = 1.0f,
     .level = Q1X15_ZERO,
     .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
     .env = {
         .a = {.duration = 0, .level = Q1X15_ONE},
         .d = {.duration = 0, .level = Q1X15_ONE},
         .s = {.duration = 0, .level = Q1X15_ONE},
         .r = {.duration = 0, .level = Q1X15_ZERO},
     }};

typedef struct audio_synth_operator_t {
  audio_synth_operator_config_t config;

  // state
  uint32_t phase;              // wave phase
  uint32_t d_phase;            // wave increment (derived from freq and mult)
  audio_synth_env_state_t env; // envelope state

  // todo: feedback
  // todo: waveform
  // todo: note velocity (?)

  audio_synth_voice_t *voice;
} audio_synth_operator_t;

typedef struct audio_synth_voice_t {
  audio_synth_operator_t ops[AUDIO_SYNTH_OPERATOR_COUNT];
  audio_synth_t *synth;
} audio_synth_voice_t;

typedef struct audio_synth_t {
  float sample_rate;
  uint32_t note_dphase_lut[128]; // mapping from MIDI note number to d_phase
  uint32_t d_timebase;           // timebase increment per sample

  q1x15 master_level;

  audio_synth_voice_t voices[AUDIO_SYNTH_VOICE_COUNT];
} audio_synth_t;

void audio_synth_init(audio_synth_t *synth, float sample_rate);

void audio_synth_operator_fill_buffer(audio_synth_operator_t *op, q1x15 *buffer,
                                      uint32_t buffer_size);

void audio_synth_voice_set_freq(audio_synth_voice_t *voice,
                                uint16_t note_number);
void audio_synth_voice_fill_buffer(audio_synth_voice_t *voice, q1x15 *buffer,
                                   uint32_t buffer_size);

void audio_synth_fill_buffer(audio_synth_t *synth, audio_buffer_t buffer,
                             uint32_t buffer_size);

// convert a note name (e.g. "C4", "A#3") to a MIDI note number
static inline uint16_t note(char *name) {
  static const char *order[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                "F#", "G",  "G#", "A",  "A#", "B"};
  int note = 0;
  for (; note < 12; note++) {
    if (strncmp(name, order[note], strlen(order[note])) == 0) {
      break;
    }
  }

  int len = strlen(name);
  int note_len = strlen(order[note]);
  int octave = 3;
  if ((len - note_len) == 1) {
    octave = name[len - 1] - '0';
  }
  int note_num = 60 + note + (octave - 4) * 12; // C4 = 60
  return note_num;
}
