#include "chart.h"

// Compile-time constant versions for static initializers
#define Q1X15_C(f) ((q1x15)((int)((f) * 32767.0)))
#define Q1X31_C(f) ((q1x31)((long long)((f) * 2147483647.0)))

// --- Demo track: "First Light" ---
// A simple introductory chart at 120 BPM with alternating lanes.
// beat_interval = 60000/120 = 500ms per beat

// Synth patches for the demo track
static const audio_song_patch_event_t first_light_patches[] = {
    {
        .patch_idx = 0,
        .patch =
            {
                .ops =
                    {
                        // lead voice: clean sine with moderate envelope
                        {
                            .freq_mult = 1,
                            .level = Q1X15_C(0.5),
                            .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
                            .env = {.a = 5, .d = 80, .s = Q1X31_C(0.6), .r = 60},
                        },
                        // slight FM for brightness
                        {
                            .freq_mult = 2,
                            .level = Q1X15_C(0.15),
                            .mode = AUDIO_SYNTH_OP_MODE_FREQ_MOD,
                            .env = {.a = 5, .d = 120, .s = Q1X31_C(0.3), .r = 80},
                        },
                        {.freq_mult = 1, .level = Q1X15_ZERO, .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE, .env = {0}},
                        {.freq_mult = 1, .level = Q1X15_ZERO, .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE, .env = {0}},
                    },
            },
    },
    {
        .patch_idx = 1,
        .patch =
            {
                .ops =
                    {
                        // bass voice
                        {
                            .freq_mult = 1,
                            .level = Q1X15_C(0.4),
                            .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
                            .env = {.a = 3, .d = 100, .s = Q1X31_C(0.5), .r = 50},
                        },
                        {.freq_mult = 1, .level = Q1X15_ZERO, .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE, .env = {0}},
                        {.freq_mult = 1, .level = Q1X15_ZERO, .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE, .env = {0}},
                        {.freq_mult = 1, .level = Q1X15_ZERO, .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE, .env = {0}},
                    },
            },
    },
};

// Song events: a simple melodic loop (120 BPM, quarter = 500ms)
// Melody: C4 E4 G4 E4 | D4 F4 A4 F4 | repeating
// Bass:   C3 on beat 1, G2 on beat 3
enum
{
    N_C3 = 48,
    N_D3 = 50,
    N_E3 = 52,
    N_F3 = 53,
    N_G3 = 55,
    N_G2 = 43,
    N_C4 = 60,
    N_D4 = 62,
    N_E4 = 64,
    N_F4 = 65,
    N_G4 = 67,
    N_A4 = 69,
};

static const audio_song_event_t first_light_events[] = {
    // Bar 1: C E G E
    {.time_ms = 0, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_C4, 90}},
    {.time_ms = 0, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_C3, 80}},
    {.time_ms = 400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_C4}},
    {.time_ms = 500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_E4, 90}},
    {.time_ms = 900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_E4}},
    {.time_ms = 1000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_G4, 90}},
    {.time_ms = 1000, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_C3}},
    {.time_ms = 1000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_G2, 80}},
    {.time_ms = 1400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_G4}},
    {.time_ms = 1500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_E4, 90}},
    {.time_ms = 1900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_E4}},
    {.time_ms = 1900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_G2}},

    // Bar 2: D F A F
    {.time_ms = 2000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_D4, 90}},
    {.time_ms = 2000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_D3, 80}},
    {.time_ms = 2400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_D4}},
    {.time_ms = 2500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_F4, 90}},
    {.time_ms = 2900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_F4}},
    {.time_ms = 3000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_A4, 90}},
    {.time_ms = 3000, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_D3}},
    {.time_ms = 3000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_G2, 80}},
    {.time_ms = 3400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_A4}},
    {.time_ms = 3500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_F4, 90}},
    {.time_ms = 3900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_F4}},
    {.time_ms = 3900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_G2}},

    // Bar 3: repeat bar 1
    {.time_ms = 4000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_C4, 90}},
    {.time_ms = 4000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_C3, 80}},
    {.time_ms = 4400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_C4}},
    {.time_ms = 4500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_E4, 90}},
    {.time_ms = 4900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_E4}},
    {.time_ms = 5000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_G4, 90}},
    {.time_ms = 5000, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_C3}},
    {.time_ms = 5000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_G2, 80}},
    {.time_ms = 5400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_G4}},
    {.time_ms = 5500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_E4, 90}},
    {.time_ms = 5900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_E4}},
    {.time_ms = 5900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_G2}},

    // Bar 4: D F A F (ending)
    {.time_ms = 6000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_D4, 90}},
    {.time_ms = 6000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_D3, 80}},
    {.time_ms = 6400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_D4}},
    {.time_ms = 6500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_F4, 90}},
    {.time_ms = 6900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_F4}},
    {.time_ms = 7000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_A4, 90}},
    {.time_ms = 7000, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_D3}},
    {.time_ms = 7000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_G2, 80}},
    {.time_ms = 7400, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_A4}},
    {.time_ms = 7500, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_F4, 90}},
    {.time_ms = 7900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_F4}},
    {.time_ms = 7900, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_G2}},

    // Final chord
    {.time_ms = 8000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {0, N_C4, 100}},
    {.time_ms = 8000, .type = AUDIO_SONG_EVENT_NOTE_ON, .data.note_on = {1, N_C3, 90}},
    {.time_ms = 9500, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {0, N_C4}},
    {.time_ms = 9500, .type = AUDIO_SONG_EVENT_NOTE_OFF, .data.note_off = {1, N_C3}},
};

static const audio_song_asset_t first_light_song = {
    .patches = first_light_patches,
    .patch_count = sizeof(first_light_patches) / sizeof(first_light_patches[0]),
    .events = first_light_events,
    .event_count = sizeof(first_light_events) / sizeof(first_light_events[0]),
    .duration_ms = 10000,
    .loop_start_ms = 0,
    .loop_end_ms = 0,
};

// Chart notes: aligned to the melody hits
// At 120 BPM, beats on 0, 500, 1000, 1500, 2000, ...
static const beatline_note_t first_light_notes[] = {
    // Bar 1
    {.hit_tick = 500, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 1000, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 1500, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 2000, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    // Bar 2
    {.hit_tick = 2500, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 3000, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 3500, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_HOLD, .hold_duration = 400},
    // Bar 3
    {.hit_tick = 4500, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 5000, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 5500, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 6000, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    // Bar 4
    {.hit_tick = 6500, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 7000, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 7500, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_HOLD, .hold_duration = 400},
    // Final
    {.hit_tick = 8500, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
};

// --- Track: "Pulse" (harder) ---

static const beatline_note_t pulse_notes[] = {
    // Eighth note patterns at 140 BPM (beat = ~428ms, eighth = ~214ms)
    {.hit_tick = 428, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 642, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 856, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 1070, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 1284, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 1498, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 1712, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_HOLD, .hold_duration = 400},
    {.hit_tick = 2140, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 2354, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 2568, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 2782, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 2996, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 3210, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 3424, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_HOLD, .hold_duration = 400},
    {.hit_tick = 3852, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 4066, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 4280, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 4494, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 4708, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_HOLD, .hold_duration = 400},
    {.hit_tick = 5136, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 5350, .lane = BEATLINE_LANE_LEFT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
    {.hit_tick = 5564, .lane = BEATLINE_LANE_RIGHT, .type = BEATLINE_NOTE_TAP, .hold_duration = 0},
};

// Reuse same song (placeholder — would have its own song in production)
static const audio_song_asset_t pulse_song = {
    .patches = first_light_patches,
    .patch_count = sizeof(first_light_patches) / sizeof(first_light_patches[0]),
    .events = first_light_events,
    .event_count = sizeof(first_light_events) / sizeof(first_light_events[0]),
    .duration_ms = 10000,
    .loop_start_ms = 0,
    .loop_end_ms = 0,
};

// --- Track registry ---
const beatline_chart_t beatline_tracks[] = {
    {
        .title = "First Light",
        .difficulty = 1,
        .bpm = 120,
        .scroll_speed = 65,
        .duration_ticks = 10000,
        .notes = first_light_notes,
        .note_count = sizeof(first_light_notes) / sizeof(first_light_notes[0]),
        .song = &first_light_song,
    },
    {
        .title = "Pulse",
        .difficulty = 3,
        .bpm = 140,
        .scroll_speed = 80,
        .duration_ticks = 10000,
        .notes = pulse_notes,
        .note_count = sizeof(pulse_notes) / sizeof(pulse_notes[0]),
        .song = &pulse_song,
    },
};

const uint8_t BEATLINE_TRACK_COUNT =
    sizeof(beatline_tracks) / sizeof(beatline_tracks[0]);
