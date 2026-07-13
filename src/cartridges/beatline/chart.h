#pragma once

#include <stdint.h>

#include <prism/audio.h>
#include <prism/sdk.h>

#include "format.h"

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

// A single note in the chart. Sorted by hit_tick ascending.
typedef struct
{
    uint32_t hit_tick;      // target judgment tick at PRISM_ENGINE_TICK_RATE
    uint8_t lane;           // BEATLINE_LANE_LEFT or BEATLINE_LANE_RIGHT
    uint8_t type;           // BEATLINE_NOTE_TAP or BEATLINE_NOTE_HOLD
    uint16_t hold_duration; // hold length in ticks (0 for taps)
} beatline_note_t;

_Static_assert(sizeof(beatline_note_t) == 8,
               ".beatline note is part of the file format");

// A song entry: shared display metadata + one source song asset.
typedef struct
{
    uint32_t pack_index;
    uint32_t file_index;
    const char *title;
    const char *display_info;
    const beatline_file_header_t *header;
    const beatline_file_patch_t *patches;
    const beatline_file_event_t *events;
    const beatline_note_t *normal_notes;
    const beatline_note_t *hard_notes;
} beatline_chart_t;

uint32_t beatline_chart_count(prism_t *prism);
bool beatline_chart_get(prism_t *prism, uint32_t index,
                        beatline_chart_t *chart);
bool beatline_chart_next(prism_t *prism, const beatline_chart_t *current,
                         beatline_chart_t *chart);
bool beatline_chart_previous(prism_t *prism,
                             const beatline_chart_t *current,
                             beatline_chart_t *chart);
bool beatline_chart_open(prism_t *prism, const beatline_chart_t *reference,
                         beatline_chart_t *chart);
const beatline_note_t *beatline_chart_notes(
    const beatline_chart_t *chart, beatline_difficulty_t difficulty,
    uint16_t *count);
uint64_t beatline_chart_ranked_id(
    const beatline_chart_t *chart, beatline_difficulty_t difficulty);
