#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <shared/utils/q1x15.h>

#include "buffer.h"
#include "synth.h"

static inline uint32_t lut_key(uint32_t phase) {
  return (phase >> (32 - AUDIO_SYNTH_LUT_RES));
}

// todo: pre-bake into a header file?
static q1x15 LUT_SINE[AUDIO_SYNTH_LUT_SIZE];
static void _fill_sine_lut() {
  for (int i = 0; i < AUDIO_SYNTH_LUT_SIZE; i++) {
    float phase = (float)i / AUDIO_SYNTH_LUT_SIZE;
    LUT_SINE[i] = q1x15_d(sin(2.0f * M_PI * phase));
  }
}

static void _fill_luts() {
  static bool luts_filled = false;
  if (luts_filled)
    return;

  _fill_sine_lut();
  luts_filled = true;
}

void audio_synth_init(audio_synth_t *synth, float sample_rate) {
  _fill_luts();

  synth->sample_rate = sample_rate;

  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++) {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    voice->synth = synth;

    // voice->ops
    for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++) {
      audio_synth_operator_t *op = &voice->ops[op_idx];
      op->config = audio_synth_operator_config_default;
      op->voice = voice;

      // Initialize operator state
      op->phase = 0;
      op->d_phase = 0;
    }
  }
}

static inline uint32_t _d_phase_from_freq(float frequency, float sample_rate) {
  return (uint32_t)((frequency / sample_rate) * (double)(1ULL << 32));
}

void audio_synth_operator_set_freq(audio_synth_operator_t *op,
                                   float frequency) {
  op->d_phase = _d_phase_from_freq(frequency * op->config.freq_mult,
                                   op->voice->synth->sample_rate);
}

void audio_synth_voice_set_freq(audio_synth_voice_t *voice, float frequency) {
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++) {
    audio_synth_operator_set_freq(&voice->ops[op_idx], frequency);
  }
}

static inline q1x15
audio_synth_operator_sample_additive(audio_synth_operator_t *op,
                                     q1x15 previous) {
  q1x15 value = LUT_SINE[lut_key(op->phase)];
  op->phase += op->d_phase;
  return q1x15_add(previous, q1x15_mul(op->config.level, value));
}

static inline q1x15
audio_synth_operator_sample_freq_mod(audio_synth_operator_t *op,
                                     q1x15 previous) {
  q1x15 value = LUT_SINE[lut_key(op->phase)];
  int mod = (int32_t)previous << 16;
  op->phase += op->d_phase + mod;
  return q1x15_mul(op->config.level, value);
}

void audio_synth_operator_fill_buffer(audio_synth_operator_t *op, q1x15 *buffer,
                                      uint32_t buffer_size) {
  // if (op->config.level == Q1X15_ZERO) {
  //   if (op->config.mode == AUDIO_SYNTH_OP_MODE_FREQ_MOD) {
  //     // freq mod operator overwrites buffer, so we clear it if we skip work
  //     memset(buffer, 0, buffer_size * sizeof(q1x15));
  //   }
  //   return;
  // }

  switch (op->config.mode) {
  case AUDIO_SYNTH_OP_MODE_ADDITIVE:
    for (uint32_t i = 0; i < buffer_size; i++)
      buffer[i] = audio_synth_operator_sample_additive(op, buffer[i]);
    break;
  case AUDIO_SYNTH_OP_MODE_FREQ_MOD:
    for (uint32_t i = 0; i < buffer_size; i++)
      buffer[i] = audio_synth_operator_sample_freq_mod(op, buffer[i]);
    break;
  }
}

void audio_synth_voice_fill_buffer(audio_synth_voice_t *voice, q1x15 *buffer,
                                   uint32_t buffer_size) {
  memset(buffer, 0, buffer_size * sizeof(q1x15));
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++) {
    audio_synth_operator_fill_buffer(&voice->ops[op_idx], buffer, buffer_size);
  }
}

static inline void _merge_drafts(q1x15 *out, q1x15 *in, uint32_t buffer_size) {
  // add draft to the buffer (hard clip at 1.0/-1.0)
  for (uint32_t i = 0; i < buffer_size; i++) {
    out[i] = q1x15_add(out[i], in[i]);
  }
}

void audio_synth_fill_buffer(audio_synth_t *synth, audio_buffer_t buffer,
                             uint32_t buffer_size) {
  static q1x15 *draft[2] = {NULL};
  static uint32_t draft_size = 0;
  if (draft_size < buffer_size) {
    for (int i = 0; i < 2; i++) {
      if (draft[i] == NULL) {
        draft[i] = malloc(buffer_size * sizeof(q1x15));
      } else {
        draft[i] = realloc(draft[i], buffer_size * sizeof(q1x15));
      }
      draft_size = buffer_size;
    }
  }

  // fill buffer with voices
  audio_synth_voice_fill_buffer(&synth->voices[0], draft[0], buffer_size);
  for (uint8_t voice_idx = 1; voice_idx < AUDIO_SYNTH_VOICE_COUNT;
       voice_idx++) {
    audio_synth_voice_fill_buffer(&synth->voices[voice_idx], draft[1],
                                  buffer_size);
    _merge_drafts(draft[0], draft[1], buffer_size);
  }

  // apply master level and write to output
  q1x15 master_level = synth->master_level;
  for (uint32_t i = 0; i < buffer_size; i++) {
    q1x15 sample = q1x15_mul(master_level, draft[0][i]);
    buffer[i] = audio_buffer_frame_from_mono((int16_t)sample);
  }
}
