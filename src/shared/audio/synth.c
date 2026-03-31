#include <assert.h>
#define _USE_MATH_DEFINES
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <platform/sync.h>
#include <platform/time.h>
#include <shared/utils/q1x15.h>
#include <shared/utils/q1x31.h>

#include "buffer.h"
#include "synth.h"

#define AUDIO_SYNTH_STOP_RELEASE_MS 5u

// Maximum buffer size across all platforms (host=512, rp2=256).
// Used to statically size the per-voice draft buffer in scratch_y.
#define AUDIO_SYNTH_MAX_BUFFER_SIZE 512

// ---------------------------------------------------------------------------
// Lookup tables — all in scratch_y for zero-wait-state access on core 1.
//
// Total scratch_y usage:
//   LUT_QUARTER_SINE_LOGMAG : 256 × 2 =  512 B
//   LUT_EXP                 : 1024 × 2 = 2048 B
//   draft_voice             : 512 × 2 =  1024 B
//                             total   =  3584 B  (< 4096 B scratch_y)
// ---------------------------------------------------------------------------

// Quarter-sine log-magnitude table.
// Entry i stores nlogq(-log₂|sin(2π·i/1024)|) for i ∈ [0, 255].
// Sign and mirror are derived from phase bits 31 and 30 at lookup time.
__attribute__((section(".scratch_y")))
static uint16_t LUT_QUARTER_SINE_LOGMAG[AUDIO_SYNTH_QUARTER_LUT_SIZE];

// Exp table: maps nlogq index → Q1.15 linear amplitude.
// LUT_EXP[i] = round(2^(−i·64/4096) × 32767)  i.e. exp(−i / 64 octaves).
// Indexed by: nlog_total >> (16 − AUDIO_SYNTH_EXP_LUT_RES)  = nlog_total >> 6
__attribute__((section(".scratch_y")))
static q1x15 LUT_EXP[AUDIO_SYNTH_EXP_LUT_SIZE];

// Per-voice draft accumulation buffer.
__attribute__((section(".scratch_y")))
static q1x15 draft_voice[AUDIO_SYNTH_MAX_BUFFER_SIZE];

// ---------------------------------------------------------------------------
// LUT key helpers
// ---------------------------------------------------------------------------

// Extract 8-bit quarter-sine key from a 32-bit phase value.
// Bit 31 = sign of sin, bit 30 = mirror flag, bits 29:22 = raw key.
// The XOR with -mirror_bit & 0xFF reverses the key for the second quarter,
// giving |sin(2π·i/1024)| = |sin(2π·(511−i)/1024)| symmetry.
static inline uint32_t quarter_lut_key(uint32_t phase)
{
  uint32_t mirror_bit = (phase >> 30) & 1u;
  uint32_t raw_key    = (phase >> 24) & 0xFFu;
  return raw_key ^ (-mirror_bit & 0xFFu);
}

// Map a 16-bit nlogq value to a 10-bit exp LUT index.
static inline uint32_t exp_lut_key(uint32_t nlog)
{
  return nlog >> (16 - AUDIO_SYNTH_EXP_LUT_RES); // >> 6  → [0, 1023]
}

// ---------------------------------------------------------------------------
// nlogq conversion helpers (not in hot loop — float arithmetic is fine here)
// ---------------------------------------------------------------------------

// Convert a Q1.31 linear level in [0, 1] to nlogq.
static inline uint16_t nlogq_from_q1x31(q1x31 level)
{
  if (level <= 0)
    return NLOGQ_SILENCE;
  float f    = (float)level * (1.0f / (float)INT32_MAX);
  float nlog = -log2f(f) * (float)NLOGQ_SCALE;
  if (nlog >= (float)NLOGQ_SILENCE)
    return NLOGQ_SILENCE;
  return (uint16_t)nlog;
}

// Convert a Q1.15 linear level in [0, 1] to nlogq.
static inline uint16_t nlogq_from_q1x15(q1x15 level)
{
  if (level <= 0)
    return NLOGQ_SILENCE;
  float f    = (float)level * (1.0f / (float)INT16_MAX);
  float nlog = -log2f(f) * (float)NLOGQ_SCALE;
  if (nlog >= (float)NLOGQ_SILENCE)
    return NLOGQ_SILENCE;
  return (uint16_t)nlog;
}

// ---------------------------------------------------------------------------
// LUT fill (called once at init)
// ---------------------------------------------------------------------------

static void _fill_quarter_sine_logmag_lut()
{
  for (int i = 0; i < AUDIO_SYNTH_QUARTER_LUT_SIZE; i++)
  {
    // i indexes the first quarter of the full 1024-entry sine table
    float phase = (float)i / (float)AUDIO_SYNTH_LUT_SIZE;
    float s     = sinf(2.0f * (float)M_PI * phase);
    float mag   = fabsf(s);
    if (mag < 1e-10f)
    {
      LUT_QUARTER_SINE_LOGMAG[i] = NLOGQ_SILENCE;
    }
    else
    {
      float nlog = -log2f(mag) * (float)NLOGQ_SCALE;
      LUT_QUARTER_SINE_LOGMAG[i] =
          nlog >= (float)NLOGQ_SILENCE ? NLOGQ_SILENCE : (uint16_t)nlog;
    }
  }
}

static void _fill_exp_lut()
{
  // Index i corresponds to nlogq = i << (16 − EXP_LUT_RES) = i * 64.
  // Amplitude = 2^(−nlogq / NLOGQ_SCALE) = 2^(−i*64/4096) = 2^(−i/64).
  for (int i = 0; i < AUDIO_SYNTH_EXP_LUT_SIZE; i++)
  {
    float amplitude = exp2f(-(float)i / 64.0f);
    LUT_EXP[i]      = q1x15_f(amplitude);
  }
}

// mapping from MIDI note number to d_phase
static void _fill_note_dphase_lut(uint32_t lut[128], float sample_rate,
                                  float a4_freq)
{
  for (int i = 0; i < 128; i++)
  {
    float frequency = powf(2.0f, (i - 69) / 12.0f) * a4_freq;
    lut[i] = (uint32_t)((frequency / sample_rate) * (double)(1ULL << 32));
  }
}

static void _fill_const_luts()
{
  static bool luts_filled = false;
  if (luts_filled)
    return;

  _fill_quarter_sine_logmag_lut();
  _fill_exp_lut();
  luts_filled = true;
}

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

void audio_synth_init(audio_synth_t *synth, float sample_rate,
                      uint32_t timebase_per_sec)
{
  _fill_const_luts();
  _fill_note_dphase_lut(synth->note_dphase_lut, sample_rate, 440.0f);

  synth->sample_rate = sample_rate;
  synth->d_timebase  = (uint32_t)(sample_rate / timebase_per_sec);

  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++)
  {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    voice->synth       = synth;
    voice->note_number = -1;
    voice->patch_idx   = 0;
    voice->on_at       = PLATFORM_TIME_ZERO;

    for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
    {
      audio_synth_operator_t *op = &voice->ops[op_idx];
      op->voice     = voice;
      op->config    = audio_synth_operator_config_default;
      op->phase     = 0;
      op->d_phase   = 0;
      op->nlog_level = NLOGQ_SILENCE;
    }
  }

  for (int patch_idx = 0; patch_idx < AUDIO_SYNTH_PATCH_COUNT; patch_idx++)
  {
    audio_synth_patch_config_t *patch = &synth->patches[patch_idx];
    for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
    {
      patch->ops[op_idx] = audio_synth_operator_config_default;
    }
  }

  synth->msg_queue = platform_queue_create(sizeof(audio_synth_message_t),
                                           AUDIO_SYNTH_MESSAGE_QUEUE_SIZE);
  synth->mutex = platform_mutex_create();
  assert(synth->msg_queue != NULL);
  assert(synth->mutex != NULL);
}

// ---------------------------------------------------------------------------
// Envelope stage construction
// ---------------------------------------------------------------------------

// Build one ADSR stage in log (nlogq) domain.
// prev_nlog / next_nlog are nlogq amplitudes (0 = peak, NLOGQ_SILENCE = silent).
// duration is in timebase units; sample_duration = duration * d_timebase.
static void make_env_stage(audio_synth_env_state_stage_t *stage,
                            uint32_t d_timebase, uint16_t duration,
                            uint16_t prev_nlog, uint16_t next_nlog)
{
  uint32_t sample_duration = (uint32_t)duration * d_timebase;
  stage->duration   = sample_duration;
  stage->nlog_level = next_nlog;

  if (sample_duration == 0)
  {
    stage->d_nlog = 0;
  }
  else
  {
    // Log-domain linear interpolation: equal dB steps per sample.
    stage->d_nlog = ((int32_t)next_nlog - (int32_t)prev_nlog) /
                    (int32_t)sample_duration;
  }
}

// ---------------------------------------------------------------------------
// Operator config / note lifecycle
// ---------------------------------------------------------------------------

void audio_synth_operator_set_config(audio_synth_operator_t *op,
                                     audio_synth_operator_config_t config)
{
  op->config = config;

  uint32_t d_timebase  = op->voice->synth->d_timebase;
  uint16_t nlog_s      = nlogq_from_q1x31(config.env.s);

  // Attack:  silence → peak (0)
  make_env_stage(&op->env.stages[0], d_timebase, config.env.a,
                 NLOGQ_SILENCE, 0);
  // Decay:   peak → sustain
  make_env_stage(&op->env.stages[1], d_timebase, config.env.d, 0, nlog_s);
  // Sustain: hold at sustain level indefinitely (UINT32_MAX = never advance)
  op->env.stages[2].duration   = UINT32_MAX;
  op->env.stages[2].d_nlog     = 0;
  op->env.stages[2].nlog_level = nlog_s;
  // Release: sustain → silence
  make_env_stage(&op->env.stages[3], d_timebase, config.env.r,
                 nlog_s, NLOGQ_SILENCE);
}

static void audio_synth_operator_note_on(audio_synth_operator_t *op,
                                         uint8_t note_number, q1x15 velocity)
{
  uint32_t lut_phase = op->voice->synth->note_dphase_lut[note_number];
  if (op->config.freq_mult == 0)
    op->d_phase = lut_phase / 2;
  else
    op->d_phase = lut_phase * op->config.freq_mult;

  q1x15 linear_level = q1x15_mul(op->config.level, velocity);
  op->nlog_level     = nlogq_from_q1x15(linear_level);

  op->env.stage      = 0;
  op->env.nlog_level = NLOGQ_SILENCE; // start silent (attack will open it)
  op->env.evolution  = 0;

  op->active = true;
}

void audio_synth_voice_note_on(audio_synth_voice_t *voice, uint8_t patch_idx,
                               uint8_t note_number, uint8_t velocity)
{
  voice->note_number = note_number;
  voice->patch_idx   = patch_idx;
  voice->on_at       = platform_now_us();
  audio_synth_patch_config_t *patch = &voice->synth->patches[patch_idx];

  q1x15 velocity_ratio = q1x15_mag(velocity, 190);

  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
  {
    audio_synth_operator_t *op = &voice->ops[op_idx];
    audio_synth_operator_set_config(op, patch->ops[op_idx]);
    audio_synth_operator_note_on(op, note_number, velocity_ratio);
  }
}

static void audio_synth_operator_note_off(audio_synth_operator_t *op)
{
  if (op->env.stage >= 3)
    return;

  op->active = false;

  // Pluck: sustain is zero so decay already heads to silence; ignore note_off.
  if (op->config.env.s == Q1X31_ZERO && op->env.stage < 2)
    return;

  // Recompute release from the current envelope level (handles early release).
  make_env_stage(&op->env.stages[3], op->voice->synth->d_timebase,
                 op->config.env.r, op->env.nlog_level, NLOGQ_SILENCE);

  op->env.stage     = 3;
  op->env.evolution = 0;
}

static void audio_synth_operator_stop(audio_synth_operator_t *op)
{
  op->active = false;

  // Fast release from current level to avoid pops.
  make_env_stage(&op->env.stages[3], op->voice->synth->d_timebase,
                 AUDIO_SYNTH_STOP_RELEASE_MS, op->env.nlog_level, NLOGQ_SILENCE);

  op->env.stage     = 3;
  op->env.evolution = 0;
}

void audio_synth_voice_note_off(audio_synth_voice_t *voice)
{
  voice->note_number = -1;
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
    audio_synth_operator_note_off(&voice->ops[op_idx]);
}

static void audio_synth_voice_stop(audio_synth_voice_t *voice)
{
  voice->note_number = -1;
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
    audio_synth_operator_stop(&voice->ops[op_idx]);
}

static void audio_synth_operator_panic(audio_synth_operator_t *op)
{
  op->nlog_level = NLOGQ_SILENCE;
  op->active     = false;
}

void audio_synth_voice_panic(audio_synth_voice_t *voice)
{
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
    audio_synth_operator_panic(&voice->ops[op_idx]);
}

// ---------------------------------------------------------------------------
// Hot loop: operator buffer fill
//
// The outer loop segments the buffer at stage boundaries so the inner loop
// contains no branches — each segment is guaranteed to complete within the
// current envelope stage.
//
// Inner loop per sample (no multiplies):
//   1. env.nlog_level += stage.d_nlog        (addition, 0 for sustain)
//   2. nlog = env.nlog_level + op.nlog_level  (addition, saturated)
//   3. key  = phase bits → quarter-sine index (bit ops)
//   4. nlog += LUT_QUARTER_SINE_LOGMAG[key]   (addition)
//   5. mag   = LUT_EXP[nlog >> 6]             (table lookup)
//   6. value = branchless_negate(mag, sign)   (XOR + add)
// ---------------------------------------------------------------------------

static void
audio_synth_operator_fill_additive(audio_synth_operator_t *restrict op,
                                   q1x15 *restrict buffer,
                                   uint32_t buffer_size)
{
  uint32_t i = 0;
  while (i < buffer_size)
  {
    audio_synth_env_state_stage_t *stage = &op->env.stages[op->env.stage];
    uint32_t remaining      = buffer_size - i;
    uint32_t stage_remaining = stage->duration - op->env.evolution;
    uint32_t seg = remaining < stage_remaining ? remaining : stage_remaining;

    int32_t  d_nlog = stage->d_nlog;
    q1x15   *bp     = buffer + i;
    q1x15   *end    = bp + seg;

    while (bp < end)
    {
      // Envelope step (d_nlog == 0 for sustain → level unchanged)
      op->env.nlog_level = (uint16_t)((int32_t)op->env.nlog_level + d_nlog);

      // Combine envelope + operator attenuation in log domain
      uint32_t nlog = (uint32_t)op->env.nlog_level + op->nlog_level;
      if (nlog > 65535u) nlog = 65535u;

      // Phase → sign bit + quarter-sine key
      uint32_t sign_bit = op->phase >> 31;
      uint32_t key      = quarter_lut_key(op->phase);
      op->phase += op->d_phase;

      // Add log-sine magnitude
      nlog += LUT_QUARTER_SINE_LOGMAG[key];
      if (nlog > 65535u) nlog = 65535u;

      // Exp lookup → linear magnitude
      q1x15 mag = LUT_EXP[exp_lut_key(nlog)];

      // Branchless sign application: negate mag when sign_bit == 1
      int16_t neg_mask = -(int16_t)sign_bit;
      q1x15   value    = (q1x15)((mag ^ neg_mask) + (int16_t)sign_bit);

      *bp = q1x15_add(*bp, value);
      bp++;
    }

    // Stage advancement — fires at most 4 times per note lifetime
    op->env.evolution += seg;
    if (op->env.evolution >= stage->duration)
    {
      op->env.nlog_level = stage->nlog_level; // snap to target (fixes rounding)
      op->env.evolution  = 0;
      op->env.stage++;
    }

    i += seg;
    if (op->env.stage >= 4)
      break;
  }
}

static void
audio_synth_operator_fill_freq_mod(audio_synth_operator_t *restrict op,
                                   q1x15 *restrict buffer,
                                   uint32_t buffer_size)
{
  uint32_t i = 0;
  while (i < buffer_size)
  {
    audio_synth_env_state_stage_t *stage = &op->env.stages[op->env.stage];
    uint32_t remaining       = buffer_size - i;
    uint32_t stage_remaining = stage->duration - op->env.evolution;
    uint32_t seg = remaining < stage_remaining ? remaining : stage_remaining;

    int32_t  d_nlog = stage->d_nlog;
    q1x15   *bp     = buffer + i;
    q1x15   *end    = bp + seg;

    while (bp < end)
    {
      op->env.nlog_level = (uint16_t)((int32_t)op->env.nlog_level + d_nlog);

      uint32_t nlog = (uint32_t)op->env.nlog_level + op->nlog_level;
      if (nlog > 65535u) nlog = 65535u;

      uint32_t sign_bit = op->phase >> 31;
      uint32_t key      = quarter_lut_key(op->phase);

      // Phase modulation: previous operator output scales phase increment
      int32_t mod = (int32_t)(*bp) << 15;
      op->phase += op->d_phase + mod;

      nlog += LUT_QUARTER_SINE_LOGMAG[key];
      if (nlog > 65535u) nlog = 65535u;

      q1x15 mag = LUT_EXP[exp_lut_key(nlog)];

      int16_t neg_mask = -(int16_t)sign_bit;
      q1x15   value    = (q1x15)((mag ^ neg_mask) + (int16_t)sign_bit);

      *bp = value; // freq_mod overwrites (its output is the modulated sine)
      bp++;
    }

    op->env.evolution += seg;
    if (op->env.evolution >= stage->duration)
    {
      op->env.nlog_level = stage->nlog_level;
      op->env.evolution  = 0;
      op->env.stage++;
    }

    i += seg;
    if (op->env.stage >= 4)
    {
      // Silence remaining buffer so downstream operators see no modulation
      memset(buffer + i, 0, (buffer_size - i) * sizeof(q1x15));
      break;
    }
  }
}

static void audio_synth_operator_fill_buffer(audio_synth_operator_t *op,
                                             q1x15 *buffer,
                                             uint32_t buffer_size)
{
  if (op->env.stage == 4 || op->nlog_level == NLOGQ_SILENCE)
  {
    if (op->config.mode == AUDIO_SYNTH_OP_MODE_FREQ_MOD)
      memset(buffer, 0, buffer_size * sizeof(q1x15));
    return;
  }

  switch (op->config.mode)
  {
  case AUDIO_SYNTH_OP_MODE_ADDITIVE:
    audio_synth_operator_fill_additive(op, buffer, buffer_size);
    break;
  case AUDIO_SYNTH_OP_MODE_FREQ_MOD:
    audio_synth_operator_fill_freq_mod(op, buffer, buffer_size);
    break;
  }
}

// ---------------------------------------------------------------------------
// Voice / synth buffer fill
// ---------------------------------------------------------------------------

static inline bool audio_synth_voice_is_silent(const audio_synth_voice_t *voice)
{
  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
  {
    if (voice->ops[op_idx].env.stage != 4)
      return false;
  }
  return true;
}

void audio_synth_voice_fill_buffer(audio_synth_voice_t *voice, q1x15 *buffer,
                                   uint32_t buffer_size)
{
  memset(buffer, 0, buffer_size * sizeof(q1x15));

  if (audio_synth_voice_is_silent(voice))
    return;

  for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
    audio_synth_operator_fill_buffer(&voice->ops[op_idx], buffer, buffer_size);
}

static inline q1x15 hard_limit_q17x15(int32_t x_q17_15)
{
  return q1x15_clamp_s32(x_q17_15);
}

void audio_synth_fill_buffer(audio_synth_t *synth, audio_buffer_t buffer,
                             uint32_t buffer_size)
{
  platform_mutex_lock(synth->mutex);

  audio_synth_message_t msg;
  while (platform_queue_try_pop(synth->msg_queue, &msg))
    audio_synth_handle_message(synth, &msg);

  assert(buffer_size <= AUDIO_SYNTH_MAX_BUFFER_SIZE);

  memset(buffer, 0, buffer_size * sizeof(int32_t));
  uint8_t active_voice_count = 0;
  for (uint8_t voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++)
  {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    if (audio_synth_voice_is_silent(voice))
      continue;

    active_voice_count++;
    audio_synth_voice_fill_buffer(voice, draft_voice, buffer_size);
    for (uint32_t i = 0; i < buffer_size; i++)
      buffer[i] += (int32_t)draft_voice[i];
  }

  q1x15 master_level = synth->master_level;
  if (active_voice_count == 0 || master_level == Q1X15_ZERO)
  {
    platform_mutex_unlock(synth->mutex);
    return;
  }

  for (uint32_t i = 0; i < buffer_size; i++)
  {
    int32_t sample = hard_limit_q17x15(buffer[i]);
    sample         = q1x15_mul(master_level, sample);
    buffer[i]      = audio_buffer_frame_from_mono((int16_t)sample);
  }

  platform_mutex_unlock(synth->mutex);
}

// ---------------------------------------------------------------------------
// Message handling / public API
// ---------------------------------------------------------------------------

void audio_synth_panic(audio_synth_t *synth)
{
  audio_synth_message_t msg;
  while (platform_queue_try_pop(synth->msg_queue, &msg))
    ;

  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++)
    audio_synth_voice_panic(&synth->voices[voice_idx]);
}

void audio_synth_note_on(audio_synth_t *synth, audio_synth_message_note_on_t msg)
{
  audio_synth_voice_t *oldest_free_voice      = NULL;
  audio_synth_voice_t *oldest_same_patch_voice = NULL;
  audio_synth_voice_t *oldest_any_voice        = NULL;
  platform_time_t oldest_free       = PLATFORM_TIME_END;
  platform_time_t oldest_same_patch = PLATFORM_TIME_END;
  platform_time_t oldest_any        = PLATFORM_TIME_END;

  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++)
  {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    if (voice->note_number == -1)
    {
      if (voice->on_at < oldest_free)
      {
        oldest_free      = voice->on_at;
        oldest_free_voice = voice;
      }
    }
    if (voice->patch_idx == msg.patch_idx)
    {
      if (voice->on_at < oldest_same_patch)
      {
        oldest_same_patch       = voice->on_at;
        oldest_same_patch_voice = voice;
      }
    }
    if (voice->on_at < oldest_any)
    {
      oldest_any      = voice->on_at;
      oldest_any_voice = voice;
    }
  }

  audio_synth_voice_t *selected_voice =
      oldest_free_voice      ? oldest_free_voice :
      oldest_same_patch_voice ? oldest_same_patch_voice :
                                oldest_any_voice;

  if (selected_voice == NULL)
  {
    printf("audio_synth_note_on: [ERR] no voice available!\n");
    selected_voice = &synth->voices[0];
  }
  audio_synth_voice_note_on(selected_voice, msg.patch_idx, msg.note_number,
                            msg.velocity);
}

void audio_synth_note_off(audio_synth_t *synth,
                          audio_synth_message_note_off_t msg)
{
  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++)
  {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    if ((msg.note_number == -1 || voice->note_number == msg.note_number) &&
        voice->patch_idx == msg.patch_idx)
      audio_synth_voice_note_off(voice);
  }
}

static void audio_synth_stop(audio_synth_t *synth,
                              audio_synth_message_stop_t msg)
{
  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++)
  {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    if ((msg.note_number == -1 || voice->note_number == msg.note_number) &&
        voice->patch_idx == msg.patch_idx)
      audio_synth_voice_stop(voice);
  }
}

void audio_synth_handle_message(audio_synth_t *synth,
                                audio_synth_message_t *msg)
{
  switch (msg->type)
  {
  case AUDIO_SYNTH_MESSAGE_NOTE_ON:
    assert(msg->data.note_on.patch_idx < AUDIO_SYNTH_PATCH_COUNT);
    audio_synth_note_on(synth, msg->data.note_on);
    break;
  case AUDIO_SYNTH_MESSAGE_NOTE_OFF:
    assert(msg->data.note_off.patch_idx < AUDIO_SYNTH_PATCH_COUNT);
    audio_synth_note_off(synth, msg->data.note_off);
    break;
  case AUDIO_SYNTH_MESSAGE_STOP:
    assert(msg->data.stop.patch_idx < AUDIO_SYNTH_PATCH_COUNT);
    audio_synth_stop(synth, msg->data.stop);
    break;
  case AUDIO_SYNTH_MESSAGE_PANIC:
    audio_synth_panic(synth);
    break;
  }
}

void audio_synth_enqueue(audio_synth_t *synth, audio_synth_message_t *msg)
{
  platform_queue_try_push(synth->msg_queue, msg);
}

void audio_synth_reset_voices(audio_synth_t *synth)
{
  for (int voice_idx = 0; voice_idx < AUDIO_SYNTH_VOICE_COUNT; voice_idx++)
  {
    audio_synth_voice_t *voice = &synth->voices[voice_idx];
    for (int op_idx = 0; op_idx < AUDIO_SYNTH_OPERATOR_COUNT; op_idx++)
      audio_synth_operator_set_config(&voice->ops[op_idx],
                                      audio_synth_operator_config_default);
  }
}

void audio_synth_patch_config_set(audio_synth_t *synth, uint8_t patch_idx,
                                  audio_synth_patch_config_t config)
{
  assert(patch_idx < AUDIO_SYNTH_PATCH_COUNT);
  platform_mutex_lock(synth->mutex);
  synth->patches[patch_idx] = config;
  platform_mutex_unlock(synth->mutex);
}
