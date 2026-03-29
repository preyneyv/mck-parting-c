#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "synth.h"

typedef enum
{
  AUDIO_SONG_EVENT_NOTE_ON = 0,
  AUDIO_SONG_EVENT_NOTE_OFF = 1,
  AUDIO_SONG_EVENT_PATCH = 2,
  AUDIO_SONG_EVENT_MARKER = 3,
  AUDIO_SONG_EVENT_TEMPO = 4,
  AUDIO_SONG_EVENT_TIMESIG = 5,
} audio_song_event_type_t;

typedef enum
{
  AUDIO_SONG_EVENT_MASK_NOTE_ON = 1u << AUDIO_SONG_EVENT_NOTE_ON,
  AUDIO_SONG_EVENT_MASK_NOTE_OFF = 1u << AUDIO_SONG_EVENT_NOTE_OFF,
  AUDIO_SONG_EVENT_MASK_PATCH = 1u << AUDIO_SONG_EVENT_PATCH,
  AUDIO_SONG_EVENT_MASK_MARKER = 1u << AUDIO_SONG_EVENT_MARKER,
  AUDIO_SONG_EVENT_MASK_TEMPO = 1u << AUDIO_SONG_EVENT_TEMPO,
  AUDIO_SONG_EVENT_MASK_TIMESIG = 1u << AUDIO_SONG_EVENT_TIMESIG,
} audio_song_event_mask_t;

#define AUDIO_SONG_EVENT_MASK_ALL ((uint32_t)0xFFFFFFFFu)

typedef struct
{
  uint8_t patch_idx;
  uint8_t note_number;
  uint8_t velocity;
} audio_song_note_on_event_t;

typedef struct
{
  uint8_t patch_idx;
  int8_t note_number; // -1 means "all notes off for this patch"
} audio_song_note_off_event_t;

typedef struct
{
  uint8_t patch_idx;
  audio_synth_patch_config_t patch;
} audio_song_patch_event_t;

typedef struct
{
  uint32_t us_per_quarter;
} audio_song_tempo_event_t;

typedef struct
{
  uint8_t numerator;
  uint8_t denominator;
} audio_song_timesig_event_t;

typedef struct
{
  uint16_t marker_id;
  uint16_t value;
} audio_song_marker_event_t;

typedef struct
{
  uint32_t time_ms;
  audio_song_event_type_t type;
  union
  {
    audio_song_note_on_event_t note_on;
    audio_song_note_off_event_t note_off;
    audio_song_patch_event_t patch;
    audio_song_tempo_event_t tempo;
    audio_song_timesig_event_t timesig;
    audio_song_marker_event_t marker;
  } data;
} audio_song_event_t;

typedef struct
{
  uint32_t bpm_q8; // integer bpm * 256
  uint8_t numerator;
  uint8_t denominator;
} audio_song_header_t;

typedef struct
{
  const audio_song_header_t *header; // optional metadata
  const audio_song_patch_event_t *patches;
  uint16_t patch_count;

  const audio_song_event_t *events;
  uint32_t event_count;

  uint32_t duration_ms;
  uint32_t loop_start_ms;
  uint32_t loop_end_ms; // 0 = use duration_ms
} audio_song_asset_t;

typedef enum
{
  AUDIO_SONG_EVENT_ACTION_PASS = 0,
  AUDIO_SONG_EVENT_ACTION_CONSUME = 1,
} audio_song_event_action_t;

typedef struct audio_song_player_t audio_song_player_t;

typedef audio_song_event_action_t (*audio_song_event_hook_t)(
    audio_song_player_t *player,
    const audio_song_event_t *event,
    void *user);

typedef struct
{
  audio_song_event_hook_t callback;
  void *user;
  uint32_t mask;
} audio_song_event_hook_desc_t;

typedef struct
{
  // Song-local patch indices are remapped as absolute = patch_base + local.
  uint8_t patch_base;
  bool loop;
  bool restart_if_playing;
} audio_song_play_options_t;

struct audio_song_player_t
{
  audio_synth_t *synth;

  const audio_song_asset_t *song;
  bool playing;
  bool paused;
  bool loop;
  uint8_t patch_base;

  uint32_t song_time_ms;
  uint32_t last_engine_ms;
  uint32_t next_event_idx;

  audio_song_event_hook_desc_t hook;
};

void audio_song_player_init(audio_song_player_t *player, audio_synth_t *synth);
void audio_song_player_set_hook(audio_song_player_t *player,
                                audio_song_event_hook_desc_t hook);
void audio_song_player_play(audio_song_player_t *player,
                            const audio_song_asset_t *song,
                            audio_song_play_options_t options,
                            uint32_t now_ms);
void audio_song_player_stop(audio_song_player_t *player, bool panic);
void audio_song_player_pause(audio_song_player_t *player);
void audio_song_player_resume(audio_song_player_t *player, uint32_t now_ms);
void audio_song_player_seek(audio_song_player_t *player,
                            uint32_t time_ms,
                            uint32_t now_ms);
void audio_song_player_tick(audio_song_player_t *player, uint32_t now_ms);

size_t audio_song_player_peek_events(const audio_song_player_t *player,
                                     uint32_t lookahead_ms,
                                     uint32_t mask,
                                     const audio_song_event_t **out_events,
                                     size_t max_events);
