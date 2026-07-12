#pragma once

#include <stdint.h>

#include <prism/audio.h>

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

typedef enum
{
    BEATLINE_DIFFICULTY_NORMAL = 0,
    BEATLINE_DIFFICULTY_HARD = 1,
} beatline_difficulty_t;

static inline uint8_t beatline_difficulty_patch(beatline_difficulty_t difficulty)
{
    return (difficulty == BEATLINE_DIFFICULTY_HARD) ? 1u : 0u;
}

// A single note in the chart. Sorted by hit_tick ascending.
typedef struct
{
    uint32_t hit_tick;      // target judgment tick at PRISM_ENGINE_TICK_RATE
    uint8_t lane;           // BEATLINE_LANE_LEFT or BEATLINE_LANE_RIGHT
    uint8_t type;           // BEATLINE_NOTE_TAP or BEATLINE_NOTE_HOLD
    uint16_t hold_duration; // hold length in ticks (0 for taps)
} beatline_note_t;

// A song entry: shared display metadata + one source song asset.
typedef struct
{
    const char *title;
    const char *display_info;
    const audio_song_asset_t *song;
} beatline_chart_t;

extern const beatline_chart_t beatline_tracks[];
extern const uint8_t BEATLINE_TRACK_COUNT;
