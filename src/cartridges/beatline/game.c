#include "game.h"

#include <stdlib.h>
#include <string.h>

#ifndef BEATLINE_LEADERBOARD_APP_ID
#define BEATLINE_LEADERBOARD_APP_ID 5
#endif
#define BEATLINE_APP_ID BEATLINE_LEADERBOARD_APP_ID

#define BEATLINE_NOTE_C3 48
#define BEATLINE_NOTE_CS3 49
#define BEATLINE_NOTE_C4 60
#define BEATLINE_NOTE_CS4 61

beatline_song_timing_t beatline_song_timing_from_asset(const audio_song_asset_t *song)
{
    beatline_song_timing_t timing = {
        .quarter_ticks = 480.0f,
        .beat_ticks = 480.0f,
        .bpm_q8 = 120u * 256u,
        .numerator = 4,
        .denominator = 4,
    };

    if (song != NULL && song->header != NULL)
    {
        if (song->header->bpm_q8 > 0)
            timing.bpm_q8 = song->header->bpm_q8;
        if (song->header->numerator > 0)
            timing.numerator = song->header->numerator;
        if (song->header->denominator > 0)
            timing.denominator = song->header->denominator;
    }

    if (timing.bpm_q8 > 0)
        timing.quarter_ticks =
            (60.0f * PRISM_ENGINE_TICK_RATE * 256.0f) /
            (float)timing.bpm_q8;

    if (song != NULL && song->events != NULL)
    {
        for (uint32_t i = 0; i < song->event_count; i++)
        {
            const audio_song_event_t *ev = &song->events[i];
            if (ev->time_ms > 0)
                break;

            if (ev->type == AUDIO_SONG_EVENT_TEMPO &&
                ev->data.tempo.us_per_quarter > 0)
            {
                timing.quarter_ticks =
                    (float)ev->data.tempo.us_per_quarter *
                    PRISM_ENGINE_TICK_RATE / 1000000.0f;
                timing.bpm_q8 = (uint32_t)(
                    (60.0f * PRISM_ENGINE_TICK_RATE * 256.0f) /
                    timing.quarter_ticks + 0.5f);
            }
            else if (ev->type == AUDIO_SONG_EVENT_TIMESIG)
            {
                if (ev->data.timesig.numerator > 0)
                    timing.numerator = ev->data.timesig.numerator;
                if (ev->data.timesig.denominator > 0)
                    timing.denominator = ev->data.timesig.denominator;
            }
        }
    }

    if (timing.denominator == 0)
        timing.denominator = 4;
    timing.beat_ticks = timing.quarter_ticks * (4.0f / (float)timing.denominator);
    if (timing.beat_ticks < 1.0f)
        timing.beat_ticks = 1.0f;

    return timing;
}

static int beatline_note_cmp_hit_tick(const void *a, const void *b)
{
    const beatline_note_t *na = (const beatline_note_t *)a;
    const beatline_note_t *nb = (const beatline_note_t *)b;

    if (na->hit_tick < nb->hit_tick)
        return -1;
    if (na->hit_tick > nb->hit_tick)
        return 1;
    return (int)na->lane - (int)nb->lane;
}

static void beatline_generate_notes_from_song(beatline_state_t *st)
{
    st->generated_note_count = 0;
    st->generated_last_note_tick = 0;
    st->generated_duration_ticks = 0;

    if (st->chart == NULL || st->chart->song == NULL ||
        st->chart->song->events == NULL)
    {
        return;
    }

    const audio_song_asset_t *song = st->chart->song;
    uint8_t rhythm_patch = beatline_difficulty_patch(st->selected_difficulty);

    bool left_hold_active = false;
    uint32_t left_hold_start = 0;
    bool right_hold_active = false;
    uint32_t right_hold_start = 0;

    for (uint32_t i = 0; i < song->event_count; i++)
    {
        const audio_song_event_t *event = &song->events[i];

        if (event->type == AUDIO_SONG_EVENT_NOTE_ON)
        {
            if (event->data.note_on.patch_idx != rhythm_patch)
                continue;

            uint8_t note = event->data.note_on.note_number;
            uint32_t t = song->event_ticks != NULL
                             ? song->event_ticks[i]
                             : prism_ticks_from_ms(event->time_ms);

            if (note == BEATLINE_NOTE_C3 || note == BEATLINE_NOTE_C4)
            {
                if (st->generated_note_count >= BEATLINE_MAX_NOTES)
                    continue;

                st->generated_notes[st->generated_note_count++] = (beatline_note_t){
                    .hit_tick = t,
                    .lane = (note == BEATLINE_NOTE_C3) ? BEATLINE_LANE_LEFT : BEATLINE_LANE_RIGHT,
                    .type = BEATLINE_NOTE_TAP,
                    .hold_duration = 0,
                };
                continue;
            }

            if (note == BEATLINE_NOTE_CS3)
            {
                left_hold_active = true;
                left_hold_start = t;
                continue;
            }

            if (note == BEATLINE_NOTE_CS4)
            {
                right_hold_active = true;
                right_hold_start = t;
                continue;
            }
        }
        else if (event->type == AUDIO_SONG_EVENT_NOTE_OFF)
        {
            if (event->data.note_off.patch_idx != rhythm_patch)
                continue;

            int8_t note = event->data.note_off.note_number;
            uint32_t t = song->event_ticks != NULL
                             ? song->event_ticks[i]
                             : prism_ticks_from_ms(event->time_ms);

            if (note == BEATLINE_NOTE_CS3 && left_hold_active)
            {
                if (st->generated_note_count >= BEATLINE_MAX_NOTES)
                {
                    left_hold_active = false;
                    continue;
                }

                uint32_t dur = (t > left_hold_start) ? (t - left_hold_start) : 0;
                if (dur > UINT16_MAX)
                    dur = UINT16_MAX;

                st->generated_notes[st->generated_note_count++] = (beatline_note_t){
                    .hit_tick = left_hold_start,
                    .lane = BEATLINE_LANE_LEFT,
                    .type = (dur > 0) ? BEATLINE_NOTE_HOLD : BEATLINE_NOTE_TAP,
                    .hold_duration = (uint16_t)dur,
                };
                left_hold_active = false;
                continue;
            }

            if (note == BEATLINE_NOTE_CS4 && right_hold_active)
            {
                if (st->generated_note_count >= BEATLINE_MAX_NOTES)
                {
                    right_hold_active = false;
                    continue;
                }

                uint32_t dur = (t > right_hold_start) ? (t - right_hold_start) : 0;
                if (dur > UINT16_MAX)
                    dur = UINT16_MAX;

                st->generated_notes[st->generated_note_count++] = (beatline_note_t){
                    .hit_tick = right_hold_start,
                    .lane = BEATLINE_LANE_RIGHT,
                    .type = (dur > 0) ? BEATLINE_NOTE_HOLD : BEATLINE_NOTE_TAP,
                    .hold_duration = (uint16_t)dur,
                };
                right_hold_active = false;
                continue;
            }
        }
    }

    // Unmatched hold starts degrade to tap notes at their NOTE_ON time.
    if (left_hold_active && st->generated_note_count < BEATLINE_MAX_NOTES)
    {
        st->generated_notes[st->generated_note_count++] = (beatline_note_t){
            .hit_tick = left_hold_start,
            .lane = BEATLINE_LANE_LEFT,
            .type = BEATLINE_NOTE_TAP,
            .hold_duration = 0,
        };
    }
    if (right_hold_active && st->generated_note_count < BEATLINE_MAX_NOTES)
    {
        st->generated_notes[st->generated_note_count++] = (beatline_note_t){
            .hit_tick = right_hold_start,
            .lane = BEATLINE_LANE_RIGHT,
            .type = BEATLINE_NOTE_TAP,
            .hold_duration = 0,
        };
    }

    qsort(st->generated_notes,
          st->generated_note_count,
          sizeof(st->generated_notes[0]),
          beatline_note_cmp_hit_tick);

    for (uint16_t i = 0; i < st->generated_note_count; i++)
    {
        const beatline_note_t *n = &st->generated_notes[i];
        uint32_t end_tick = n->hit_tick + n->hold_duration;
        if (end_tick > st->generated_last_note_tick)
            st->generated_last_note_tick = end_tick;
    }

    st->generated_duration_ticks = song->duration_ticks != 0
                                       ? song->duration_ticks
                                       : prism_ticks_from_ms(song->duration_ms);
    if (st->generated_last_note_tick > st->generated_duration_ticks)
        st->generated_duration_ticks = st->generated_last_note_tick;
}

static audio_song_event_action_t beatline_song_event_hook(
    audio_song_player_t *player,
    const audio_song_event_t *event,
    void *user)
{
    (void)player;
    (void)user;
    // filter out note events on rhythm patches (0 = normal, 1 = hard)

    if (event->type == AUDIO_SONG_EVENT_NOTE_ON &&
        event->data.note_on.patch_idx <= 1)
    {
        return AUDIO_SONG_EVENT_ACTION_CONSUME;
    }

    if (event->type == AUDIO_SONG_EVENT_NOTE_OFF &&
        event->data.note_off.patch_idx <= 1)
    {
        return AUDIO_SONG_EVENT_ACTION_CONSUME;
    }

    return AUDIO_SONG_EVENT_ACTION_PASS;
}

// --- Helpers ---

int32_t beatline_game_time_signed(const beatline_state_t *st)
{
    if (st->game_start_tick == 0)
        return 0;
    return (int32_t)(prism_ticks(beatline_prism) - st->game_start_tick) + st->av_offset_ticks;
}

static int32_t beatline_game_time_at_tick(const beatline_state_t *st,
                                          uint32_t tick)
{
    if (st->game_start_tick == 0)
        return 0;
    return (int32_t)(tick - st->game_start_tick) + st->av_offset_ticks;
}

uint32_t beatline_game_time(const beatline_state_t *st)
{
    int32_t t = beatline_game_time_signed(st);
    if (t < 0)
        return 0;
    return (uint32_t)t;
}

beatline_song_timing_t beatline_song_timing(const beatline_state_t *st)
{
    if (st == NULL || st->chart == NULL)
        return beatline_song_timing_from_asset(NULL);
    return beatline_song_timing_from_asset(st->chart->song);
}

uint16_t beatline_combo_multiplier(uint16_t combo)
{
    if (combo >= BEATLINE_COMBO_TIER3)
        return 8;
    if (combo >= BEATLINE_COMBO_TIER2)
        return 4;
    if (combo >= BEATLINE_COMBO_TIER1)
        return 2;
    return 1;
}

const char *beatline_grade_str(beatline_grade_t grade)
{
    switch (grade)
    {
    case BEATLINE_GRADE_PERFECT:
        return "PERFECT";
    case BEATLINE_GRADE_GOOD:
        return "GOOD";
    case BEATLINE_GRADE_BAD:
        return "BAD";
    case BEATLINE_GRADE_MISS:
        return "MISS";
    default:
        return "";
    }
}

const char *beatline_rank_str(beatline_rank_t rank)
{
    switch (rank)
    {
    case BEATLINE_RANK_S:
        return "S";
    case BEATLINE_RANK_A:
        return "A";
    case BEATLINE_RANK_B:
        return "B";
    case BEATLINE_RANK_C:
        return "C";
    case BEATLINE_RANK_D:
        return "D";
    default:
        return "?";
    }
}

beatline_rank_t beatline_game_rank(const beatline_state_t *st)
{
    uint16_t total = 0;
    for (int i = 0; i < BEATLINE_GRADE_COUNT; i++)
        total += st->grade_counts[i];

    if (total == 0)
        return BEATLINE_RANK_D;

    // S: all GOOD or PERFECT (no BAD/MISS)
    if (st->grade_counts[BEATLINE_GRADE_BAD] == 0 &&
        st->grade_counts[BEATLINE_GRADE_MISS] == 0)
        return BEATLINE_RANK_S;

    uint16_t good_or_better =
        st->grade_counts[BEATLINE_GRADE_PERFECT] +
        st->grade_counts[BEATLINE_GRADE_GOOD];

    // A: >=90%
    if (good_or_better * 100 >= total * 90)
        return BEATLINE_RANK_A;

    // B: >=85%
    if (good_or_better * 100 >= total * 85)
        return BEATLINE_RANK_B;

    // C: >=70%
    if (good_or_better * 100 >= total * 70)
        return BEATLINE_RANK_C;

    return BEATLINE_RANK_D;
}

// --- Init ---

void beatline_game_init(beatline_state_t *st)
{
    memset(st, 0, sizeof(*st));
    st->screen = BEATLINE_SCREEN_SELECT;
    st->selected_difficulty = BEATLINE_DIFFICULTY_NORMAL;
    memset(st->note_grades, BEATLINE_NOTE_UNJUDGED, sizeof(st->note_grades));
}

void beatline_game_select_track(beatline_state_t *st, int8_t track_idx,
                                beatline_difficulty_t difficulty)
{
    st->chart = &beatline_tracks[track_idx];
    st->selected_track = track_idx;
    st->selected_difficulty = difficulty;

    // reset play state
    st->score = 0;
    st->combo = 0;
    st->max_combo = 0;
    st->next_judge_idx = 0;
    st->generated_note_count = 0;
    st->generated_last_note_tick = 0;
    st->generated_duration_ticks = 0;
    st->game_start_tick = 0;
    st->av_offset_ticks = BEATLINE_DEFAULT_AV_OFFSET_TICKS;
    memset(st->grade_counts, 0, sizeof(st->grade_counts));
    memset(st->note_grades, BEATLINE_NOTE_UNJUDGED, sizeof(st->note_grades));
    memset(st->note_score_multipliers, 0,
           sizeof(st->note_score_multipliers));
    memset(st->hold_state, 0, sizeof(st->hold_state));
    memset(st->feedback, 0, sizeof(st->feedback));
    st->qr_ready = false;
}

// --- Countdown ---

void beatline_game_start_countdown(beatline_state_t *st)
{
    beatline_song_timing_t timing = beatline_song_timing(st);
    uint32_t beat_interval = (uint32_t)(timing.beat_ticks + 0.5f);
    if (beat_interval == 0)
        beat_interval = 1;

    // Set game_start_tick 4 beats in the future so notes pre-roll
    // visibly during the count-in.
    uint32_t count_in_ticks = 4 * beat_interval;
    st->game_start_tick = prism_ticks(beatline_prism) + count_in_ticks;
    st->grid_offset = 0;

    // Generate notes now so they appear scrolling during the count-in.
    beatline_generate_notes_from_song(st);
    memset(st->note_grades, BEATLINE_NOTE_UNJUDGED, sizeof(st->note_grades));
    memset(st->note_score_multipliers, 0,
           sizeof(st->note_score_multipliers));
    st->next_judge_idx = 0;
    memset(st->hold_state, 0, sizeof(st->hold_state));

    // Count-in state — beat number displayed as overlay on the play screen.
    st->countdown_beat = 4;
    st->countdown_next_tick = prism_ticks(beatline_prism) + beat_interval;

    // Show the play screen immediately so the player sees the notes scroll in.
    st->screen = BEATLINE_SCREEN_PLAY;

    // Init audio player with hook but don't start playback yet;
    // beatline_game_start_play() triggers it when the count-in ends.
    audio_song_player_init(&st->song_player, prism_synth(beatline_prism));
    audio_song_player_set_hook(
        &st->song_player,
        (audio_song_event_hook_desc_t){
            .callback = beatline_song_event_hook,
            .user = st,
            .mask = AUDIO_SONG_EVENT_MASK_NOTE_ON | AUDIO_SONG_EVENT_MASK_NOTE_OFF,
        });
}

void beatline_game_start_play(beatline_state_t *st)
{
    st->countdown_beat = 0;

    audio_song_player_play(
        &st->song_player,
        st->chart->song,
        (audio_song_play_options_t){
            .patch_base = 0,
            .loop = false,
            .restart_if_playing = true,
        },
        prism_millis(beatline_prism));
}

// --- Judgment ---

static void register_grade(beatline_state_t *st, uint16_t note_idx,
                           beatline_grade_t grade,
                           int32_t timing_delta_ticks)
{
    st->note_grades[note_idx] = (uint8_t)grade;
    st->grade_counts[grade]++;

    uint8_t lane = st->generated_notes[note_idx].lane;
    uint16_t awarded_score = 0;

    // scoring
    if (grade == BEATLINE_GRADE_MISS || grade == BEATLINE_GRADE_BAD)
    {
        if (grade == BEATLINE_GRADE_BAD)
            awarded_score = BEATLINE_SCORE_BAD;
        st->combo = 0;
    }
    else
    {
        st->combo++;
        if (st->combo > st->max_combo)
            st->max_combo = st->combo;

        uint16_t mult = beatline_combo_multiplier(st->combo);
        uint16_t base = (grade == BEATLINE_GRADE_PERFECT)
                            ? BEATLINE_SCORE_PERFECT
                            : BEATLINE_SCORE_GOOD;
        awarded_score = (uint16_t)(base * mult);
        st->note_score_multipliers[note_idx] = (uint8_t)mult;
    }
    st->score += awarded_score;

    // feedback
    st->feedback[lane].grade = grade;
    st->feedback[lane].timing_late = (timing_delta_ticks < 0);
    st->feedback[lane].until_tick = prism_ticks(beatline_prism) + BEATLINE_FEEDBACK_DURATION;
}

static void register_early_hold_release(beatline_state_t *st,
                                        uint16_t note_idx)
{
    beatline_grade_t previous_grade =
        (beatline_grade_t)st->note_grades[note_idx];
    uint8_t lane = st->generated_notes[note_idx].lane;

    if (previous_grade != BEATLINE_GRADE_BAD)
    {
        if (previous_grade < BEATLINE_GRADE_COUNT &&
            st->grade_counts[previous_grade] > 0)
        {
            st->grade_counts[previous_grade]--;
        }
        st->grade_counts[BEATLINE_GRADE_BAD]++;

        uint16_t previous_base =
            previous_grade == BEATLINE_GRADE_PERFECT
                ? BEATLINE_SCORE_PERFECT
                : BEATLINE_SCORE_GOOD;
        uint16_t previous_award =
            previous_base * st->note_score_multipliers[note_idx];
        if (st->score >= previous_award)
            st->score -= previous_award;
        else
            st->score = 0;
        st->score += BEATLINE_SCORE_BAD;
        st->note_score_multipliers[note_idx] = 0;
        st->note_grades[note_idx] = BEATLINE_GRADE_BAD;
    }

    st->combo = 0;
    st->feedback[lane].grade = BEATLINE_GRADE_BAD;
    st->feedback[lane].timing_late = false;
    st->feedback[lane].until_tick =
        prism_ticks(beatline_prism) + BEATLINE_FEEDBACK_DURATION;
}

static void register_empty_miss(beatline_state_t *st, uint8_t lane)
{
    st->grade_counts[BEATLINE_GRADE_MISS]++;
    st->combo = 0;

    st->feedback[lane].grade = BEATLINE_GRADE_MISS;
    st->feedback[lane].timing_late = false;
    st->feedback[lane].until_tick = prism_ticks(beatline_prism) + BEATLINE_FEEDBACK_DURATION;
}

static beatline_grade_t judge_timing(int32_t delta_ticks)
{
    if (delta_ticks < 0)
        delta_ticks = -delta_ticks;

    if (delta_ticks <= BEATLINE_WINDOW_PERFECT)
        return BEATLINE_GRADE_PERFECT;
    if (delta_ticks <= BEATLINE_WINDOW_GOOD)
        return BEATLINE_GRADE_GOOD;
    if (delta_ticks <= BEATLINE_WINDOW_BAD)
        return BEATLINE_GRADE_BAD;

    return BEATLINE_GRADE_MISS;
}

static void try_hit_lane(beatline_state_t *st, uint8_t lane,
                         uint32_t input_tick)
{
    int32_t signed_game_time = beatline_game_time_at_tick(st, input_tick);
    uint32_t game_time = signed_game_time < 0 ? 0u : (uint32_t)signed_game_time;

    // scan for the closest unjudged note in this lane within the BAD window
    int32_t best_delta = INT32_MAX;
    int16_t best_idx = -1;

    for (uint16_t i = 0; i < st->generated_note_count; i++)
    {
        if (st->note_grades[i] != BEATLINE_NOTE_UNJUDGED)
            continue;
        if (st->generated_notes[i].lane != lane)
            continue;

        int32_t delta = (int32_t)st->generated_notes[i].hit_tick - (int32_t)game_time;

        // too far in the future
        if (delta > BEATLINE_WINDOW_BAD)
            break;
        // too far in the past
        if (delta < -BEATLINE_WINDOW_BAD)
            continue;

        int32_t abs_delta = delta < 0 ? -delta : delta;
        if (abs_delta < best_delta)
        {
            best_delta = abs_delta;
            best_idx = (int16_t)i;
        }
    }

    if (best_idx >= 0)
    {
        int32_t delta = (int32_t)st->generated_notes[best_idx].hit_tick - (int32_t)game_time;
        beatline_grade_t grade = judge_timing(delta);
        register_grade(st, (uint16_t)best_idx, grade, delta);

        // start hold tracking if hold note
        if (st->generated_notes[best_idx].type == BEATLINE_NOTE_HOLD &&
            grade != BEATLINE_GRADE_MISS)
        {
            st->hold_state[lane].holding = true;
            st->hold_state[lane].note_idx = (uint16_t)best_idx;
        }

        return;
    }
    // no note nearby — count as miss
    register_empty_miss(st, lane);
}

static void process_misses(beatline_state_t *st)
{
    uint32_t game_time = beatline_game_time(st);

    while (st->next_judge_idx < st->generated_note_count)
    {
        uint16_t i = st->next_judge_idx;

        if (st->note_grades[i] != BEATLINE_NOTE_UNJUDGED)
        {
            st->next_judge_idx++;
            continue;
        }

        int32_t delta = (int32_t)game_time - (int32_t)st->generated_notes[i].hit_tick;
        if (delta > BEATLINE_WINDOW_BAD)
        {
            register_grade(st, i, BEATLINE_GRADE_MISS, -(int32_t)delta);
            st->next_judge_idx++;
            continue;
        }
        break;
    }
}

static void process_holds(beatline_state_t *st)
{
    for (uint8_t lane = 0; lane < 2; lane++)
    {
        if (!st->hold_state[lane].holding)
            continue;

        uint16_t idx = st->hold_state[lane].note_idx;
        const beatline_note_t *n = &st->generated_notes[idx];
        uint32_t end_tick = n->hit_tick + n->hold_duration;

        prism_button_t btn = (lane == BEATLINE_LANE_LEFT)
                                 ? PRISM_BUTTON_LEFT
                                 : PRISM_BUTTON_RIGHT;

        uint32_t sample_tick = prism_button_keyup(beatline_prism, btn)
                                   ? prism_button_keyup_tick(beatline_prism, btn)
                                   : prism_ticks(beatline_prism);
        int32_t signed_game_time =
            beatline_game_time_at_tick(st, sample_tick);
        uint32_t game_time =
            signed_game_time < 0 ? 0u : (uint32_t)signed_game_time;

        if (!prism_button_pressed(beatline_prism, btn))
        {
            // released early — if significantly early, penalize
            if (game_time + BEATLINE_HOLD_RELEASE_GRACE < end_tick)
            {
                register_early_hold_release(st, idx);
            }
            st->hold_state[lane].holding = false;
        }
        else if (game_time >= end_tick)
        {
            // held past end — success, stop tracking
            st->hold_state[lane].holding = false;
        }
    }
}

// --- Tick ---

void beatline_game_tick(beatline_state_t *st)
{
    if (st->screen != BEATLINE_SCREEN_PLAY)
        return;

    // Count-in pre-roll: block input/judging until the 4 beats elapse.
    if (st->countdown_beat > 0)
    {
        audio_song_player_tick(&st->song_player, prism_millis(beatline_prism));
        if (prism_ticks(beatline_prism) >= st->countdown_next_tick)
        {
            st->countdown_beat--;
            if (st->countdown_beat == 0)
            {
                beatline_game_start_play(st);
                return;
            }
            beatline_song_timing_t timing = beatline_song_timing(st);
            uint32_t beat_interval = (uint32_t)(timing.beat_ticks + 0.5f);
            if (beat_interval == 0)
                beat_interval = 1;
            st->countdown_next_tick += beat_interval;
        }
        return;
    }

    // advance song
    audio_song_player_tick(&st->song_player, prism_millis(beatline_prism));

    // handle input
    if (prism_button_keydown(beatline_prism, PRISM_BUTTON_LEFT))
        try_hit_lane(st, BEATLINE_LANE_LEFT,
                     prism_button_keydown_tick(beatline_prism,
                                                PRISM_BUTTON_LEFT));
    if (prism_button_keydown(beatline_prism, PRISM_BUTTON_RIGHT))
        try_hit_lane(st, BEATLINE_LANE_RIGHT,
                     prism_button_keydown_tick(beatline_prism,
                                                PRISM_BUTTON_RIGHT));

    process_misses(st);
    process_holds(st);

    uint32_t game_time = beatline_game_time(st);

    // clear expired feedback
    for (uint8_t lane = 0; lane < 2; lane++)
    {
        if (st->feedback[lane].until_tick != 0 &&
            prism_ticks(beatline_prism) >= st->feedback[lane].until_tick)
        {
            st->feedback[lane].until_tick = 0;
        }
    }

    // check natural end
    if (game_time >= st->generated_duration_ticks)
    {
        // wait a little after last note for feedback to show
        uint32_t last_note_tick = st->generated_last_note_tick;
        if (game_time >= last_note_tick + 1440)
            beatline_game_finish(st);
    }
}

void beatline_game_finish(beatline_state_t *st)
{
    st->screen = BEATLINE_SCREEN_RESULTS;
    audio_song_player_stop(&st->song_player, true);

    // judge any remaining notes as miss
    for (uint16_t i = 0; i < st->generated_note_count; i++)
    {
        if (st->note_grades[i] == BEATLINE_NOTE_UNJUDGED)
            register_grade(st, i, BEATLINE_GRADE_MISS, 0);
    }

    uint8_t stats[14];
    stats[0] = (uint8_t)(((uint8_t)st->selected_track << 1) |
                         ((uint8_t)st->selected_difficulty & 0x01u));
    stats[1] = (uint8_t)beatline_game_rank(st);
    stats[2] = (uint8_t)(st->score & 0xFF);
    stats[3] = (uint8_t)((st->score >> 8) & 0xFF);
    stats[4] = (uint8_t)((st->score >> 16) & 0xFF);
    stats[5] = (uint8_t)((st->score >> 24) & 0xFF);
    stats[6] = (uint8_t)(st->max_combo & 0xFF);
    stats[7] = (uint8_t)((st->max_combo >> 8) & 0xFF);
    stats[8] = (uint8_t)(st->grade_counts[BEATLINE_GRADE_PERFECT] & 0xFF);
    stats[9] = (uint8_t)((st->grade_counts[BEATLINE_GRADE_PERFECT] >> 8) & 0xFF);
    stats[10] = (uint8_t)(st->grade_counts[BEATLINE_GRADE_GOOD] & 0xFF);
    stats[11] = (uint8_t)((st->grade_counts[BEATLINE_GRADE_GOOD] >> 8) & 0xFF);
    stats[12] = (uint8_t)(st->grade_counts[BEATLINE_GRADE_BAD] & 0xFF);
    stats[13] = (uint8_t)(st->grade_counts[BEATLINE_GRADE_MISS] & 0xFF);
    st->qr_ready = prism_leaderboard_qrcode(
        beatline_prism, BEATLINE_APP_ID, stats, sizeof(stats), st->qr_code);

    prism_buttons_reset(beatline_prism);
}
