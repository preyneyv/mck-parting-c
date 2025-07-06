#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#include <shared/utils/q1x15.h>

#include "buffer.h"
#include "synth.h"

void audio_synth_init(audio_synth_t *synth, float sample_rate) {
  synth->sample_rate = sample_rate;

  for (int i = 0; i < AUDIO_SYNTH_MAX_VOICES; i++) {
    audio_synth_voice_t *voice = &synth->voices[i];
    voice->synth = synth;

    voice->is_fm = false;
    voice->op1.freq_mult = 1.0f;
    voice->op1.phase = 0;
    voice->op1.d_phase = 0;
    voice->op1.level = q1x15_f(1.0f);

    voice->op2.freq_mult = 1.0f;
    voice->op2.phase = 0;
    voice->op2.d_phase = 0;
    voice->op2.level = q1x15_f(1.0f);
  }
}

static inline uint32_t _freq_to_phase_inc(float frequency, float sample_rate) {
  return (uint32_t)((frequency / sample_rate) * (double)(1ULL << 32));
}

void audio_synth_voice_set_freq(audio_synth_voice_t *voice, float frequency) {
  // Set the frequency of the voice
  voice->op1.d_phase = _freq_to_phase_inc(frequency * voice->op1.freq_mult,
                                          voice->synth->sample_rate);
  voice->op2.d_phase = _freq_to_phase_inc(frequency * voice->op2.freq_mult,
                                          voice->synth->sample_rate);
}

void audio_synth_operator_fill_buffer(audio_synth_operator_t *op, q1x15 *buffer,
                                      uint32_t buffer_size) {
  for (uint32_t i = 0; i < buffer_size; i++) {

    // todo: use lookup table
    float phase_evolution =
        ((float)(op->phase >> 24) / UINT8_MAX); // top 8 bits
    q1x15 value = q1x15_f(sinf(2.0f * M_PI * phase_evolution));
    buffer[i] = q1x15_mul(op->level, value);

    op->phase += op->d_phase;
  }
}

void audio_synth_voice_fill_buffer(audio_synth_voice_t *voice, q1x15 *buffer,
                                   uint32_t buffer_size) {
  // todo: FM with op1 -> op2
  audio_synth_operator_fill_buffer(&voice->op2, buffer, buffer_size);
}

static inline void draft_merge_clip(q1x15 *out, q1x15 *in,
                                    uint32_t buffer_size) {
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

  audio_synth_voice_fill_buffer(&synth->voices[0], draft[0], buffer_size);
  // for (uint8_t voice_idx = 1; voice_idx < AUDIO_SYNTH_MAX_VOICES;
  // voice_idx++) {
  //   audio_synth_voice_fill_buffer(&synth->voices[voice_idx], draft[1],
  //                                 buffer_size);
  //   draft_merge_clip(draft[0], draft[1], buffer_size);
  // }

  // copy the final draft to the output buffer
  for (uint32_t i = 0; i < buffer_size; i++) {
    buffer[i] = audio_buffer_frame_from_mono((int16_t)draft[0][i]);
  }
}
