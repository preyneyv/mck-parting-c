// Inspired by the Yamaha OPL-II and similar FM synthesizers.
// Supports additive and frequency modulation synthesis.
// Uses fixed-point arithmetic for audio processing to be fast on MCUs.

// todo: synth -> [instrument] -> voice -> operator
//                   |- add this layer for polyphony.
//                   |- voices/ops on an instrument have the same parameters.

#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <pico/sync.h>
#include <pico/util/queue.h>

#include <shared/utils/q1x15.h>
#include <shared/utils/q1x31.h>

#include "buffer.h"

#define AUDIO_SYNTH_VOICE_COUNT 8
#define AUDIO_SYNTH_OPERATOR_COUNT 4
#define AUDIO_SYNTH_LUT_RES 10
#define AUDIO_SYNTH_LUT_SIZE (1 << AUDIO_SYNTH_LUT_RES)
#define AUDIO_SYNTH_MESSAGE_QUEUE_SIZE 32

typedef struct audio_synth_t audio_synth_t;
typedef struct audio_synth_voice_t audio_synth_voice_t;

typedef enum {
  AUDIO_SYNTH_MESSAGE_NOTE_ON,  // play a note on a voice
  AUDIO_SYNTH_MESSAGE_NOTE_OFF, // release a voice
  AUDIO_SYNTH_MESSAGE_PANIC,    // stop all voices
} audio_synth_message_type_t;

typedef struct audio_synth_message_note_on_t {
  uint8_t voice;        // voice index (0-3)
  uint16_t note_number; // MIDI note number (0-127)
  uint8_t velocity;     // velocity (0-127)
} audio_synth_message_note_on_t;

typedef struct audio_synth_message_note_off_t {
  uint8_t voice; // voice index (0-3)
} audio_synth_message_note_off_t;

typedef struct audio_synth_message_panic_t {
  // no data for panic
} audio_synth_message_panic_t;
typedef struct audio_synth_message_t {
  audio_synth_message_type_t type;
  union {
    audio_synth_message_note_on_t note_on;
    audio_synth_message_note_off_t note_off;
    audio_synth_message_panic_t panic;
  } data;
} audio_synth_message_t;

typedef enum {
  // add operator to previous output
  AUDIO_SYNTH_OP_MODE_ADDITIVE,
  // use previous output as freq mod input
  AUDIO_SYNTH_OP_MODE_FREQ_MOD
} audio_synth_operator_mode_t;

typedef enum {
  AUDIO_SYNTH_OP_WAVEFORM_SINE,
} audio_synth_operator_waveform_t;

typedef struct audio_synth_env_config_t {
  uint32_t a; // attack duration in timebase
  uint32_t d; // decay duration in timebase
  q1x31 s;    // sustain level (0 = pluck)
  uint32_t r; // release duration in timebase
} audio_synth_env_config_t;

typedef struct audio_synth_operator_config_t {
  int freq_mult;                    // frequency multiplier (0 = 0.5x, 3 = 3x)
  q1x15 level;                      // output level
  audio_synth_operator_mode_t mode; // operator mode
  audio_synth_env_config_t env;     // envelope config
  // todo: waveform
} audio_synth_operator_config_t;

static const audio_synth_operator_config_t audio_synth_operator_config_default =
    {.freq_mult = 1,
     .level = Q1X15_ZERO,
     .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
     .env = {
         .a = 0,
         .d = 0,
         .s = Q1X31_ONE,
         .r = 0,
     }};

typedef struct audio_synth_env_state_stage_t {
  uint32_t duration; // sample count in this stage
  q1x31 d_level;     // change per sample in this stage
  q1x31 level;       // target level for this stage
} audio_synth_env_state_stage_t;

typedef struct audio_synth_env_state_t {
  q1x31 level;        // current level
  uint32_t evolution; // evolution in sample count
  uint8_t stage;      // current stage (0 = A, 1 = D, 2 = S, 3 = R)
  audio_synth_env_state_stage_t stages[4]; // stages for A, D, 0, R
} audio_synth_env_state_t;

typedef struct audio_synth_operator_t {
  audio_synth_operator_config_t config;

  // state
  uint32_t phase;              // wave phase
  uint32_t d_phase;            // wave increment (derived from freq and mult)
  audio_synth_env_state_t env; // envelope state
  q1x15 level;                 // output level (after velocity)
  bool active;                 // is this operator active?

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
  uint32_t d_timebase;           // samples per timebase unit

  q1x15 master_level;

  audio_synth_voice_t voices[AUDIO_SYNTH_VOICE_COUNT];

  queue_t msg_queue; // message queue for thread-safe operation
  mutex_t mutex;     // mutex for any thread-safe operations
} audio_synth_t;

// thread-safe update operator values based on active config
void audio_synth_operator_set_config(audio_synth_operator_t *op,
                                     audio_synth_operator_config_t config);

// turn on a note for a voice
void audio_synth_voice_note_on(audio_synth_voice_t *voice, uint16_t note_number,
                               uint8_t velocity);
// turn off a note for a voice
void audio_synth_voice_note_off(audio_synth_voice_t *voice);
// panic a voice (immediately stop operators)
void audio_synth_voice_panic(audio_synth_voice_t *voice);

// fill a buffer with samples from a voice (internal)
void audio_synth_voice_fill_buffer(audio_synth_voice_t *voice, q1x15 *buffer,
                                   uint32_t buffer_size);

// initialize the audio synthesizer
// - sample_rate: sample rate in Hz
// - timebase: timebase in Hz (1000 = 1000 ticks / second)
void audio_synth_init(audio_synth_t *synth, float sample_rate,
                      uint32_t timebase);

// panic the synthesizer (stop all voices)
void audio_synth_panic(audio_synth_t *synth);

// handle a message for the synthesizer
void audio_synth_handle_message(audio_synth_t *synth,
                                audio_synth_message_t *msg);

// thread-safe, core-safe enqueue a message for the synthesizer
// messages may be dropped if the queue is full
void audio_synth_enqueue(audio_synth_t *synth, audio_synth_message_t *msg);

// fill a buffer with samples from the synthesizer
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
