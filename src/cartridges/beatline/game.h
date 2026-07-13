#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <qrcodegen.h>
#include <prism/sdk.h>
#include "chart.h"

extern prism_t *beatline_prism;

// --- Judgment ---

typedef enum
{
    BEATLINE_GRADE_PERFECT = 0,
    BEATLINE_GRADE_GOOD = 1,
    BEATLINE_GRADE_BAD = 2,
    BEATLINE_GRADE_MISS = 3,
} beatline_grade_t;

#define BEATLINE_GRADE_COUNT 4

// Timing windows around hit_tick, converted from authored milliseconds.
#define BEATLINE_WINDOW_PERFECT 58
#define BEATLINE_WINDOW_GOOD 96
#define BEATLINE_WINDOW_BAD 135
#define BEATLINE_HOLD_RELEASE_GRACE 77

// Negative values delay game-time relative to wall clock to compensate output/input latency.
#define BEATLINE_DEFAULT_AV_OFFSET_TICKS 19

// --- Scoring ---

#define BEATLINE_SCORE_PERFECT 300
#define BEATLINE_SCORE_GOOD 100
#define BEATLINE_SCORE_BAD 50

// Combo multiplier thresholds
#define BEATLINE_COMBO_TIER1 10 // 2x
#define BEATLINE_COMBO_TIER2 30 // 4x
#define BEATLINE_COMBO_TIER3 70 // 8x

// --- Rank ---

typedef enum
{
    BEATLINE_RANK_S = 0,
    BEATLINE_RANK_A,
    BEATLINE_RANK_B,
    BEATLINE_RANK_C,
    BEATLINE_RANK_D,
} beatline_rank_t;

// --- Layout ---

#define BEATLINE_HIT_ZONE_W 10
#define BEATLINE_PLAY_FIELD_X BEATLINE_HIT_ZONE_W
#define BEATLINE_PLAY_FIELD_W (DISP_WIDTH - 2 * BEATLINE_HIT_ZONE_W)
#define BEATLINE_LANE_DIVIDER_Y 32

// --- Screens ---

typedef enum
{
    BEATLINE_SCREEN_SELECT,
    BEATLINE_SCREEN_PLAY,
    BEATLINE_SCREEN_RESULTS,
} beatline_screen_t;

// Per-note judgment result (0xff = unjudged)
#define BEATLINE_NOTE_UNJUDGED 0xFF

// Max notes we can track (sized for compiled charts)
#define BEATLINE_MAX_NOTES 1024

// Feedback display duration (ticks)
#define BEATLINE_FEEDBACK_DURATION 288
// LED flash duration (ticks)
#define BEATLINE_LED_DURATION 96

// --- Game State ---

typedef struct
{
    float quarter_ticks;
    float beat_ticks;
    uint32_t bpm_q8;
    uint8_t numerator;
    uint8_t denominator;
} beatline_song_timing_t;

typedef struct
{
    beatline_screen_t screen;

    // selection
    int8_t selected_track;
    beatline_difficulty_t selected_difficulty;
    int32_t select_scroll_y; // animated scroll offset

    // chart reference
    const beatline_chart_t *chart;

    // song playback
    audio_song_player_t song_player;
    uint32_t game_start_tick;
    int32_t av_offset_ticks; // audio-visual offset, default 0

    // note tracking
    beatline_note_t generated_notes[BEATLINE_MAX_NOTES];
    uint16_t generated_note_count;
    uint32_t generated_last_note_tick;
    uint32_t generated_duration_ticks;
    uint16_t next_judge_idx; // next unjudged note (for miss scanning)
    uint8_t note_grades[BEATLINE_MAX_NOTES];
    uint8_t note_score_multipliers[BEATLINE_MAX_NOTES];

    // hold state per lane
    struct
    {
        bool holding;
        uint16_t note_idx;
    } hold_state[2];

    // scoring
    uint32_t score;
    uint16_t combo;
    uint16_t max_combo;
    uint16_t grade_counts[BEATLINE_GRADE_COUNT];

    // leaderboard export
    leaderboard_qrcode_t qr_code;
    bool qr_ready;

    // per-lane feedback
    struct
    {
        beatline_grade_t grade;
        bool timing_late;
        uint32_t until_tick;
    } feedback[2];

    // countdown
    uint8_t countdown_beat; // 3, 2, 1, 0 (0 = done)
    uint32_t countdown_next_tick;

    // background grid scroll offset (animated)
    int32_t grid_offset;
} beatline_state_t;

// --- API ---

void beatline_game_init(beatline_state_t *st);
void beatline_game_select_track(beatline_state_t *st, int8_t track_idx,
                                beatline_difficulty_t difficulty);
void beatline_game_start_countdown(beatline_state_t *st);
void beatline_game_start_play(beatline_state_t *st);
void beatline_game_tick(beatline_state_t *st);
void beatline_game_finish(beatline_state_t *st);

// Query helpers
int32_t beatline_game_time_signed(const beatline_state_t *st);
uint32_t beatline_game_time(const beatline_state_t *st);
beatline_song_timing_t beatline_song_timing(const beatline_state_t *st);
beatline_song_timing_t beatline_song_timing_from_asset(const audio_song_asset_t *song);
beatline_rank_t beatline_game_rank(const beatline_state_t *st);
const char *beatline_grade_str(beatline_grade_t grade);
const char *beatline_rank_str(beatline_rank_t rank);
uint16_t beatline_combo_multiplier(uint16_t combo);
