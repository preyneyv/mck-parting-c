#pragma once

#include <platform/sync.h>
#include <platform/time.h>

#include "buffer.h"
#include "synth.h"

#define AUDIO_SYNTH_VOICE_COUNT 12
#define AUDIO_SYNTH_LUT_RES 10
#define AUDIO_SYNTH_LUT_SIZE (1 << AUDIO_SYNTH_LUT_RES)
#define AUDIO_SYNTH_MESSAGE_QUEUE_SIZE 64
#define AUDIO_SYNTH_MAX_MESSAGES_PER_BUFFER 16

typedef struct
{
  uint32_t duration;
  q1x31 d_level;
  q1x31 level;
} audio_synth_env_state_stage_t;

typedef struct
{
  q1x31 level;
  uint32_t evolution;
  uint8_t stage;
  audio_synth_env_state_stage_t stages[4];
} audio_synth_env_state_t;

typedef struct audio_synth_voice audio_synth_voice_t;

typedef struct
{
  audio_synth_operator_config_t config;
  uint32_t phase;
  uint32_t d_phase;
  audio_synth_env_state_t env;
  q1x15 level;
  audio_synth_voice_t *voice;
} audio_synth_operator_t;

struct audio_synth_voice
{
  audio_synth_operator_t ops[AUDIO_SYNTH_OPERATOR_COUNT];
  uint8_t active_op_mask;
  int8_t note_number;
  platform_time_t on_at;
  uint8_t patch_idx;
  audio_synth_t *synth;
};

struct audio_synth_t
{
  float sample_rate;
  uint32_t note_dphase_lut[128];
  uint32_t d_timebase;
  q1x15 master_level;
  audio_synth_voice_t voices[AUDIO_SYNTH_VOICE_COUNT];
  audio_synth_patch_config_t patches[AUDIO_SYNTH_PATCH_COUNT];
  platform_queue_t *msg_queue;
  platform_mutex_t *mutex;
};

void audio_synth_init(audio_synth_t *synth, float sample_rate,
                      uint32_t timebase);
void audio_synth_reset(audio_synth_t *synth);
void audio_synth_panic_sync(audio_synth_t *synth);
void audio_synth_fill_buffer(audio_synth_t *synth, audio_buffer_t buffer,
                             uint32_t buffer_size);
void audio_synth_set_master_level(audio_synth_t *synth, q1x15 level);
