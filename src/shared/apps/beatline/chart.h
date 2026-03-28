#pragma once

#include <stdint.h>

#include <shared/audio/song.h>

typedef enum
{
    BEATLINE_LANE_LEFT = 0,
    BEATLINE_LANE_RIGHT = 1,
} beatline_lane_t;

typedef enum
{
    BEATLINE_NOTE_TAP = 0,
    BEATLINE_NOTE_HOLD = 1,
} beatline_note_type_t;

// A single note in the chart. Sorted by hit_tick ascending.
typedef struct
{
    uint32_t hit_tick;      // target judgment tick (ms, since tick rate=1000Hz)
    uint8_t lane;           // BEATLINE_LANE_LEFT or BEATLINE_LANE_RIGHT
    uint8_t type;           // BEATLINE_NOTE_TAP or BEATLINE_NOTE_HOLD
    uint16_t hold_duration; // hold length in ticks (0 for taps)
} beatline_note_t;

// A complete chart: metadata + note data + song reference.
typedef struct
{
    const char *title;
    uint8_t difficulty; // 1-5 stars
    uint16_t bpm;
    uint16_t scroll_speed;   // pixels per second
    uint32_t duration_ticks; // total chart length in ticks
    const beatline_note_t *notes;
    uint16_t note_count;
    const audio_song_asset_t *song;
} beatline_chart_t;

extern const beatline_chart_t beatline_tracks[];
extern const uint8_t BEATLINE_TRACK_COUNT;
