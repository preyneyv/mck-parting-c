#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <shared/utils/q1x15.h>
#include <shared/utils/q1x31.h>

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

// mapping from MIDI note number to d_phase
static void _fill_note_dphase_lut(uint32_t lut[128], float sample_rate,
                                  float a4_freq) {
  for (int i = 0; i < 128; i++) {
    float frequency = powf(2.0f, (i - 69) / 12.0f) * a4_freq;
    lut[i] = (uint32_t)((frequency / sample_rate) * (double)(1ULL << 32));
  }
}

static void _fill_const_luts() {
  static bool luts_filled = false;
  if (luts_filled)
    return;

  _fill_sine_lut();
  luts_filled = true;
}

void audio_synth_init(audio_synth_t *synth, float sample_rate,
                      uint32_t timebase_per_sec) {
  _fill_const_luts();
  _fill_note_dphase_lut(synth->note_dphase_lut, sample_rate, 440.0f);

  synth->sample_rate = sample_rate;
  synth->d_timebase = (uint32_t)(sample_rate / timebase_per_sec);

  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++) {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    voice->synth = synth;

    for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++) {
      audio_synth_operator_t *op = &voice->ops[op_idx];
      op->voice = voice;

      op->config = audio_synth_operator_config_default;

      // Initialize operator state
      op->phase = 0;
      op->d_phase = 0;
      op->level = Q1X15_ZERO;
    }
  }
}

static void make_env_stage_from_cfg(audio_synth_env_state_stage_t *stage,
                                    uint32_t d_timebase, uint16_t duration,
                                    q1x31 prev_level, q1x31 next_level) {
  uint16_t sample_duration = duration * d_timebase;
  printf("make_env_stage_from_cfg: duration=%d, prev_level=%d, next_level=%d, "
         "%d\n",
         sample_duration, prev_level, next_level, next_level - prev_level);
  stage->duration = sample_duration;
  stage->level = next_level;

  if (sample_duration == 0) {
    // if duration is zero, we just set the level to the next level
    stage->d_level = Q1X31_ZERO;
  } else {
    stage->d_level = (next_level - prev_level) / (int32_t)sample_duration;
  }
}

// update operator values based on active config
void audio_synth_operator_set_config(audio_synth_operator_t *op,
                                     audio_synth_operator_config_t config) {
  op->config = config;

  // update envelope timing
  uint32_t d_timebase = op->voice->synth->d_timebase;
  make_env_stage_from_cfg(&op->env.stages[0], d_timebase, config.env.a,
                          Q1X31_ZERO, Q1X31_ONE);
  make_env_stage_from_cfg(&op->env.stages[1], d_timebase, config.env.d,
                          Q1X31_ONE, config.env.s);
  make_env_stage_from_cfg(&op->env.stages[2], d_timebase, 0, config.env.s,
                          config.env.s);
  make_env_stage_from_cfg(&op->env.stages[3], d_timebase, config.env.r,
                          config.env.s, Q1X31_ZERO);
  printf("d_timebase: %d\n", d_timebase);
  printf("op %p config: freq_mult=%d, level=%d, mode=%d, env=(%d, %d, %d, "
         "%d)\n",
         (void *)op, config.freq_mult, config.level, config.mode,
         op->env.stages[0].d_level, op->env.stages[1].d_level,
         op->env.stages[2].level, op->env.stages[3].d_level);
}

static void audio_synth_operator_note_on(audio_synth_operator_t *op,
                                         uint16_t note_number, q1x15 velocity) {
  // this *might* be called without a previous note_off

  op->phase = 0;
  op->d_phase =
      op->voice->synth->note_dphase_lut[note_number] * op->config.freq_mult;
  op->level = q1x15_mul(op->config.level, velocity);

  // reset envelope
  op->env.stage = 0;          // reset to attack stage
  op->env.level = Q1X31_ZERO; // reset envelope level
  op->env.evolution = 0;      // reset evolution

  op->active = true;
}

void audio_synth_voice_note_on(audio_synth_voice_t *voice, uint16_t note_number,
                               uint8_t velocity) {
  // set frequency
  q1x15 velocity_ratio = q1x15_mag(velocity, 127);

  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++) {
    audio_synth_operator_t *op = &voice->ops[op_idx];
    audio_synth_operator_note_on(op, note_number, velocity_ratio);
  }
}

static void audio_synth_operator_note_off(audio_synth_operator_t *op) {
  op->active = false;
}

void audio_synth_voice_note_off(audio_synth_voice_t *voice) {
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++) {
    audio_synth_operator_t *op = &voice->ops[op_idx];
    audio_synth_operator_note_off(op);
  }
}

static void audio_synth_operator_panic(audio_synth_operator_t *op) {
  op->level = Q1X15_ZERO;
  op->active = false;
}

void audio_synth_voice_panic(audio_synth_voice_t *voice) {
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++) {
    audio_synth_operator_t *op = &voice->ops[op_idx];
    audio_synth_operator_panic(op);
  }
}

static q1x15 audio_synth_operator_update_env(audio_synth_operator_t *op) {
  audio_synth_env_state_t *env = &op->env;
  audio_synth_env_state_stage_t *stage = &env->stages[env->stage];
  if (env->stage == 4)
    return Q1X15_ZERO; // already post release
  if (env->stage == 2) {
    if (op->active) {
      // hold sustain level
      env->level = stage->level;
    } else {
      // move to release
      env->stage = 3;
      env->evolution = 0;
    }
  } else {
    env->evolution += 1;

    if (env->evolution < stage->duration) {
      env->level += stage->d_level; // update level
    } else {
      env->level = stage->level; // jump to target level
      env->evolution = 0;        // reset evolution
      env->stage++;              // move to next stage
    }
  }

  return q1x15_mul(op->level, q1x31_to_q1x15(env->level));
}

static inline q1x15
audio_synth_operator_sample_additive(audio_synth_operator_t *op,
                                     q1x15 previous) {
  q1x15 mult = audio_synth_operator_update_env(op);
  q1x15 value = LUT_SINE[lut_key(op->phase)];
  op->phase += op->d_phase;
  return q1x15_add(previous, q1x15_mul(value, mult));
}

static inline q1x15
audio_synth_operator_sample_freq_mod(audio_synth_operator_t *op,
                                     q1x15 previous) {
  q1x15 mult = audio_synth_operator_update_env(op);
  q1x15 value = LUT_SINE[lut_key(op->phase)];
  int mod = (int32_t)previous << 16;
  op->phase += op->d_phase + mod;
  return q1x15_mul(value, mult);
}

static void audio_synth_operator_fill_buffer(audio_synth_operator_t *op,
                                             q1x15 *buffer,
                                             uint32_t buffer_size) {
  // todo: reenable this? saves work on idle ops but might cause stutters when
  // instruments come in.

  // if (op->level == Q1X15_ZERO) {
  //   if (op->config.mode == AUDIO_SYNTH_OP_MODE_FREQ_MOD) {
  //     // freq mod operator overwrites buffer, so we clear it if we skip
  //     work memset(buffer, 0, buffer_size * sizeof(q1x15));
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

void audio_synth_panic(audio_synth_t *synth) {
  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++) {
    audio_synth_voice_panic(&synth->voices[voice_idx]);
  }
}

void audio_synth_handle_message(audio_synth_t *synth,
                                audio_synth_message_t *msg) {
  // printf("audio_synth_handle_message: %d\n", msg->type);
  switch (msg->type) {
  case AUDIO_SYNTH_MESSAGE_NOTE_ON: {
    assert(msg->data.note_on.voice < AUDIO_SYNTH_VOICE_COUNT);
    audio_synth_voice_note_on(&synth->voices[msg->data.note_on.voice],
                              msg->data.note_on.note_number,
                              msg->data.note_on.velocity);
    break;
  }
  case AUDIO_SYNTH_MESSAGE_NOTE_OFF: {
    assert(msg->data.note_off.voice < AUDIO_SYNTH_VOICE_COUNT);
    audio_synth_voice_note_off(&synth->voices[msg->data.note_off.voice]);
    break;
  }
  case AUDIO_SYNTH_MESSAGE_PANIC: {
    audio_synth_panic(synth);
    break;
  }
  }
}
