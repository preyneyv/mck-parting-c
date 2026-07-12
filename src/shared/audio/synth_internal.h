#pragma once

#include <platform/sync.h>
#include <platform/time.h>

#include "buffer.h"
#include "synth.h"

#define AUDIO_SYNTH_VOICE_COUNT 12
#define AUDIO_SYNTH_ANALYSIS_SAMPLE_COUNT 1024
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
  uint16_t message_queue_depth;
  uint16_t message_queue_peak;
  uint32_t message_queue_full_count;
  uint16_t active_voice_mask;
  uint16_t analysis_peak;
  uint32_t analysis_late_count;
  uint32_t analysis_underrun_count;
  bool analysis_requested;
  bool analysis_active;
  uint8_t analysis_write_index;
  uint16_t analysis_write_offset;
  uint32_t analysis_generation;
  uint32_t analysis_published;
  int16_t analysis_samples[2][AUDIO_SYNTH_ANALYSIS_SAMPLE_COUNT];
};

void audio_synth_init(audio_synth_t *synth, float sample_rate,
                      uint32_t timebase);
void audio_synth_reset(audio_synth_t *synth);
void audio_synth_panic_sync(audio_synth_t *synth);
void audio_synth_fill_buffer(audio_synth_t *synth, audio_buffer_t buffer,
                             uint32_t buffer_size);
void audio_synth_set_master_level(audio_synth_t *synth, q1x15 level);
void audio_synth_analysis_set_enabled(audio_synth_t *synth, bool enabled);
uint16_t audio_synth_active_voice_mask(const audio_synth_t *synth);
uint16_t audio_synth_analysis_take_peak(audio_synth_t *synth);
bool audio_synth_analysis_snapshot(
    const audio_synth_t *synth,
    int16_t samples[AUDIO_SYNTH_ANALYSIS_SAMPLE_COUNT], uint32_t *sequence);
bool audio_synth_analysis_enabled(const audio_synth_t *synth);
uint16_t audio_synth_message_queue_depth(const audio_synth_t *synth);
uint16_t audio_synth_message_queue_take_peak(audio_synth_t *synth);
uint32_t audio_synth_message_queue_full_count(const audio_synth_t *synth);
uint32_t audio_synth_analysis_late_count(const audio_synth_t *synth);
uint32_t audio_synth_analysis_underrun_count(const audio_synth_t *synth);
void audio_synth_analysis_report_late(audio_synth_t *synth);
void audio_synth_analysis_report_underrun(audio_synth_t *synth);
