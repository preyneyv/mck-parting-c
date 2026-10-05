#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <shared/utils/q1x15.h>
#include <shared/utils/q1x31.h>

#define AUDIO_SYNTH_OPERATOR_COUNT 4
#define AUDIO_SYNTH_PATCH_COUNT 32
/* Cartridge-authored patches occupy 0-15. The OS owns 16-31 so system UI
 * sounds remain available regardless of the active cartridge. */
#define AUDIO_SYNTH_CARTRIDGE_PATCH_COUNT 16
#define AUDIO_SYNTH_SYSTEM_PATCH_FIRST AUDIO_SYNTH_CARTRIDGE_PATCH_COUNT

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

void audio_synth_patch_config_set(audio_synth_t *synth, uint8_t patch_idx,
                                  audio_synth_patch_config_t config);

/* Convert a note name such as "C4" or "A#3" to a MIDI note number (C4 = 60).
 * Without an octave digit, octave 3 is assumed. */
static inline uint16_t note(char *name)
{
  static const char *order[] = {"C", "C#", "D", "D#", "E", "F",
                                "F#", "G", "G#", "A", "A#", "B"};
  int index = 0;
  for (; index < 12; index++)
  {
    if (strncmp(name, order[index], strlen(order[index])) == 0)
      break;
  }

  int len = strlen(name);
  int name_len = strlen(order[index]);
  int octave = 3;
  if ((len - name_len) == 1)
    octave = name[len - 1] - '0';
  return 60 + index + (octave - 4) * 12;
}
