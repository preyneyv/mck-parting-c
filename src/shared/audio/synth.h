#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <shared/utils/q1x15.h>
#include <shared/utils/q1x31.h>

#define AUDIO_SYNTH_OPERATOR_COUNT 4
#define AUDIO_SYNTH_PATCH_COUNT 32

typedef struct audio_synth_t audio_synth_t;

typedef enum
{
  AUDIO_SYNTH_MESSAGE_NOTE_ON,  // play a note on a voice
  AUDIO_SYNTH_MESSAGE_NOTE_OFF, // release a voice
  AUDIO_SYNTH_MESSAGE_STOP,     // short release stop for targeted voices
  AUDIO_SYNTH_MESSAGE_PANIC,    // stop all voices
} audio_synth_message_type_t;

typedef struct audio_synth_message_note_on_t
{
  uint8_t patch_idx;   // patch index (0-31)
  uint8_t note_number; // MIDI note number (0-127)
  uint8_t velocity;    // velocity (0-127)
} audio_synth_message_note_on_t;

typedef struct audio_synth_message_note_off_t
{
  uint8_t patch_idx;  // patch index (0-31)
  int8_t note_number; // MIDI note number (0-127) (-1 = all notes)
} audio_synth_message_note_off_t;

typedef struct audio_synth_message_panic_t
{
  // no data for panic
} audio_synth_message_panic_t;

typedef struct audio_synth_message_stop_t
{
  uint8_t patch_idx;  // patch index (0-31)
  int8_t note_number; // MIDI note number (0-127) (-1 = all notes on patch)
} audio_synth_message_stop_t;

typedef struct audio_synth_message_t
{
  audio_synth_message_type_t type;
  union
  {
    audio_synth_message_note_on_t note_on;
    audio_synth_message_note_off_t note_off;
    audio_synth_message_stop_t stop;
    audio_synth_message_panic_t panic;
  } data;
} audio_synth_message_t;

typedef enum
{
  // add operator to previous output
  AUDIO_SYNTH_OP_MODE_ADDITIVE = 0,
  // use previous output as freq mod input
  AUDIO_SYNTH_OP_MODE_FREQ_MOD = 1,
} audio_synth_operator_mode_t;

typedef enum
{
  AUDIO_SYNTH_OP_WAVEFORM_SINE,
} audio_synth_operator_waveform_t;

typedef struct audio_synth_env_config_t
{
  uint32_t a; // attack duration in timebase
  uint32_t d; // decay duration in timebase
  q1x31 s;    // sustain level (0 = pluck)
  uint32_t r; // release duration in timebase
} audio_synth_env_config_t;

typedef struct audio_synth_operator_config_t
{
  int freq_mult;                    // frequency multiplier (0 = 0.5x, 3 = 3x)
  q1x15 level;                      // output level
  audio_synth_operator_mode_t mode; // operator mode
  audio_synth_env_config_t env;     // envelope config
  // todo: waveform
} audio_synth_operator_config_t;

typedef struct audio_synth_patch_config_t
{
  audio_synth_operator_config_t ops[AUDIO_SYNTH_OPERATOR_COUNT];
} audio_synth_patch_config_t;

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

static const audio_synth_patch_config_t audio_synth_patch_config_default =
    {.ops = {
         audio_synth_operator_config_default,
         audio_synth_operator_config_default,
         audio_synth_operator_config_default,
         audio_synth_operator_config_default,
     }};

bool audio_synth_enqueue(audio_synth_t *synth,
                         const audio_synth_message_t *msg);

// set a patch configuration
void audio_synth_patch_config_set(audio_synth_t *synth, uint8_t patch_idx,
                                  audio_synth_patch_config_t config);

// convert a note name (e.g. "C4", "A#3") to a MIDI note number
static inline uint16_t note(char *name)
{
  static const char *order[] = {"C", "C#", "D", "D#", "E", "F",
                                "F#", "G", "G#", "A", "A#", "B"};
  int note = 0;
  for (; note < 12; note++)
  {
    if (strncmp(name, order[note], strlen(order[note])) == 0)
    {
      break;
    }
  }

  int len = strlen(name);
  int note_len = strlen(order[note]);
  int octave = 3;
  if ((len - note_len) == 1)
  {
    octave = name[len - 1] - '0';
  }
  int note_num = 60 + note + (octave - 4) * 12; // C4 = 60
  return note_num;
}
