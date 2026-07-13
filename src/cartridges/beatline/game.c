#include "game.h"

#include <stdlib.h>
#include <string.h>

#ifndef BEATLINE_LEADERBOARD_APP_ID
#define BEATLINE_LEADERBOARD_APP_ID 5
#endif
#define BEATLINE_APP_ID BEATLINE_LEADERBOARD_APP_ID

beatline_song_timing_t beatline_song_timing_from_chart(
    const beatline_chart_t *chart)
{
    beatline_song_timing_t timing = {
        .quarter_ticks = 480.0f,
        .beat_ticks = 480.0f,
        .bpm_q8 = 120u * 256u,
        .numerator = 4,
        .denominator = 4,
    };

    if (chart != NULL && chart->header != NULL)
    {
        timing.bpm_q8 = chart->header->bpm_q8;
        timing.numerator = chart->header->numerator;
        timing.denominator = chart->header->denominator;
    }

    if (timing.bpm_q8 > 0)
        timing.quarter_ticks =
            (60.0f * PRISM_ENGINE_TICK_RATE * 256.0f) /
            (float)timing.bpm_q8;

    if (timing.denominator == 0)
        timing.denominator = 4;
    timing.beat_ticks = timing.quarter_ticks * (4.0f / (float)timing.denominator);
    if (timing.beat_ticks < 1.0f)
        timing.beat_ticks = 1.0f;

    return timing;
}

static audio_synth_patch_config_t patch_config(
    const beatline_file_patch_t *patch)
{
    audio_synth_patch_config_t config = {0};
    for (uint8_t i = 0; i < AUDIO_SYNTH_OPERATOR_COUNT; ++i)
    {
        const beatline_file_operator_t *source = &patch->ops[i];
        config.ops[i] = (audio_synth_operator_config_t){
            .freq_mult = source->freq_mult,
            .level = source->level,
            .mode = (audio_synth_operator_mode_t)source->mode,
            .env = {
                .a = source->attack,
                .d = source->decay,
                .s = source->sustain,
                .r = source->release,
            },
        };
    }
    return config;
}

static void song_stop_patches(beatline_song_player_t *player)
{
    if (player == NULL || player->chart == NULL)
        return;
    for (uint32_t i = 0; i < player->chart->header->patch_count; ++i)
    {
        audio_synth_enqueue(
            prism_synth(beatline_prism),
            &(audio_synth_message_t){
                .type = AUDIO_SYNTH_MESSAGE_STOP,
                .data.stop = {
                    .patch_idx = player->chart->patches[i].patch_idx,
                    .note_number = -1,
                },
            });
    }
}

void beatline_song_player_stop(beatline_song_player_t *player, bool panic)
{
    if (player == NULL)
        return;
    song_stop_patches(player);
    if (panic)
    {
        audio_synth_enqueue(prism_synth(beatline_prism),
                            &(audio_synth_message_t){
                                .type = AUDIO_SYNTH_MESSAGE_PANIC,
                            });
    }
    player->playing = false;
    player->paused = false;
    player->chart = NULL;
    player->song_time_ms = 0;
    player->last_engine_ms = 0;
    player->next_event_idx = 0;
}

void beatline_song_player_pause(beatline_song_player_t *player)
{
    if (player == NULL || !player->playing)
        return;
    song_stop_patches(player);
    player->paused = true;
}

void beatline_song_player_resume(beatline_song_player_t *player,
                                 uint32_t now_ms)
{
    if (player == NULL || !player->playing)
        return;
    player->paused = false;
    player->last_engine_ms = now_ms;
}

static bool song_dispatch(const beatline_file_event_t *event)
{
    audio_synth_message_t message = {0};
    if (event->type == BEATLINE_FILE_EVENT_NOTE_ON)
    {
        message.type = AUDIO_SYNTH_MESSAGE_NOTE_ON;
        message.data.note_on = (audio_synth_message_note_on_t){
            .patch_idx = event->patch_idx,
            .note_number = event->note,
            .velocity = event->velocity,
        };
    }
    else
    {
        message.type = AUDIO_SYNTH_MESSAGE_NOTE_OFF;
        message.data.note_off = (audio_synth_message_note_off_t){
            .patch_idx = event->patch_idx,
            .note_number = (int8_t)event->note,
        };
    }
    return audio_synth_enqueue(prism_synth(beatline_prism), &message);
}

static void song_tick(beatline_song_player_t *player, uint32_t now_ms)
{
    if (player == NULL || !player->playing || player->paused ||
        player->chart == NULL)
        return;
    player->song_time_ms += now_ms - player->last_engine_ms;
    player->last_engine_ms = now_ms;
    while (player->next_event_idx < player->chart->header->event_count)
    {
        const beatline_file_event_t *event =
            &player->chart->events[player->next_event_idx];
        if (event->time_ms > player->song_time_ms || !song_dispatch(event))
            break;
        ++player->next_event_idx;
    }
}

static void song_play(beatline_song_player_t *player,
                      const beatline_chart_t *chart, uint32_t now_ms)
{
    beatline_song_player_stop(player, false);
    memset(player, 0, sizeof(*player));
    player->chart = chart;
    player->playing = true;
    player->last_engine_ms = now_ms;
    for (uint32_t i = 0; i < chart->header->patch_count; ++i)
        audio_synth_patch_config_set(
            prism_synth(beatline_prism), chart->patches[i].patch_idx,
            patch_config(&chart->patches[i]));
    song_tick(player, now_ms);
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
    if (st == NULL || st->chart.header == NULL)
        return beatline_song_timing_from_chart(NULL);
    return beatline_song_timing_from_chart(&st->chart);
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
    st->track_count = beatline_chart_count(beatline_prism);
    st->selected_difficulty = BEATLINE_DIFFICULTY_NORMAL;
    memset(st->note_grades, BEATLINE_NOTE_UNJUDGED, sizeof(st->note_grades));
}

bool beatline_game_select_chart(beatline_state_t *st,
                                const beatline_chart_t *chart,
                                beatline_difficulty_t difficulty)
{
    beatline_chart_t opened;
    if (chart == NULL ||
        !beatline_chart_open(beatline_prism, chart, &opened))
        return false;
    st->chart = opened;
    st->selected_difficulty = difficulty;

    // reset play state
    st->score = 0;
    st->combo = 0;
    st->max_combo = 0;
    st->next_judge_idx = 0;
    st->notes = beatline_chart_notes(&st->chart, difficulty,
                                     &st->note_count);
    st->last_note_tick = 0;
    for (uint16_t i = 0; i < st->note_count; ++i)
    {
        uint32_t end = st->notes[i].hit_tick + st->notes[i].hold_duration;
        if (end > st->last_note_tick)
            st->last_note_tick = end;
    }
    st->duration_ticks = st->chart.header->duration_ticks;
    if (st->last_note_tick > st->duration_ticks)
        st->duration_ticks = st->last_note_tick;
    st->game_start_tick = 0;
    st->av_offset_ticks = BEATLINE_DEFAULT_AV_OFFSET_TICKS;
    memset(st->grade_counts, 0, sizeof(st->grade_counts));
    memset(st->note_grades, BEATLINE_NOTE_UNJUDGED, sizeof(st->note_grades));
    memset(st->note_score_multipliers, 0,
           sizeof(st->note_score_multipliers));
    memset(st->hold_state, 0, sizeof(st->hold_state));
    memset(st->feedback, 0, sizeof(st->feedback));
    st->qr_ready = false;
    return true;
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

    // The XIP-backed audio player starts when count-in reaches zero.
    memset(&st->song_player, 0, sizeof(st->song_player));
}

void beatline_game_start_play(beatline_state_t *st)
{
    st->countdown_beat = 0;

    song_play(&st->song_player, &st->chart, prism_millis(beatline_prism));
}

// --- Judgment ---

static void register_grade(beatline_state_t *st, uint16_t note_idx,
                           beatline_grade_t grade,
                           int32_t timing_delta_ticks)
{
    st->note_grades[note_idx] = (uint8_t)grade;
    st->grade_counts[grade]++;

    uint8_t lane = st->notes[note_idx].lane;
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
    uint8_t lane = st->notes[note_idx].lane;

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

    for (uint16_t i = 0; i < st->note_count; i++)
    {
        if (st->note_grades[i] != BEATLINE_NOTE_UNJUDGED)
            continue;
        if (st->notes[i].lane != lane)
            continue;

        int32_t delta = (int32_t)st->notes[i].hit_tick - (int32_t)game_time;

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
        int32_t delta = (int32_t)st->notes[best_idx].hit_tick - (int32_t)game_time;
        beatline_grade_t grade = judge_timing(delta);
        register_grade(st, (uint16_t)best_idx, grade, delta);

        // start hold tracking if hold note
        if (st->notes[best_idx].type == BEATLINE_NOTE_HOLD &&
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

    while (st->next_judge_idx < st->note_count)
    {
        uint16_t i = st->next_judge_idx;

        if (st->note_grades[i] != BEATLINE_NOTE_UNJUDGED)
        {
            st->next_judge_idx++;
            continue;
        }

        int32_t delta = (int32_t)game_time - (int32_t)st->notes[i].hit_tick;
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
        const beatline_note_t *n = &st->notes[idx];
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
        song_tick(&st->song_player, prism_millis(beatline_prism));
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
    song_tick(&st->song_player, prism_millis(beatline_prism));

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
    if (game_time >= st->duration_ticks)
    {
        // wait a little after last note for feedback to show
        uint32_t last_note_tick = st->last_note_tick;
        if (game_time >= last_note_tick + 1440)
            beatline_game_finish(st);
    }
}

void beatline_game_finish(beatline_state_t *st)
{
    st->screen = BEATLINE_SCREEN_RESULTS;
    beatline_song_player_stop(&st->song_player, true);

    // judge any remaining notes as miss
    for (uint16_t i = 0; i < st->note_count; i++)
    {
        if (st->note_grades[i] == BEATLINE_NOTE_UNJUDGED)
            register_grade(st, i, BEATLINE_GRADE_MISS, 0);
    }

    uint64_t chart_id = beatline_chart_ranked_id(
        &st->chart, st->selected_difficulty);
    uint8_t stats[BEATLINE_LEADERBOARD_PAYLOAD_BYTES];
    beatline_leaderboard_payload(
        stats, chart_id, st->score, st->max_combo,
        st->grade_counts[BEATLINE_GRADE_PERFECT],
        st->grade_counts[BEATLINE_GRADE_GOOD],
        st->grade_counts[BEATLINE_GRADE_BAD],
        st->grade_counts[BEATLINE_GRADE_MISS]);
    st->qr_ready = chart_id != 0 &&
                   prism_leaderboard_qrcode(
                       beatline_prism, BEATLINE_APP_ID, stats,
                       sizeof(stats), st->qr_code);

    prism_buttons_reset(beatline_prism);
}
