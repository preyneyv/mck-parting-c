#include <stdio.h>
#include <string.h>

#include <shared/anim.h>
#include <shared/apps/apps.h>
#include <shared/config.h>
#include <shared/engine.h>
#include <platform/display.h>
#include <shared/utils/elm.h>
#include <shared/utils/led_anim.h>
#include <shared/utils/misc.h>
#include <shared/utils/vec.h>

#include "chart.h"
#include "game.h"

static beatline_state_t state;

// ============================================================
// Track Selection Screen
// ============================================================

#define SELECT_CARD_W (DISP_WIDTH - 2)
#define SELECT_CARD_H (DISP_HEIGHT - 2)
#define SELECT_CARD_GAP 0
#define SELECT_CARD_Y 1
#define SELECT_STAR_W 5
#define SELECT_HOLD_BOX_W 78
#define SELECT_HOLD_BOX_H 11

static const int16_t SELECT_SCROLL_MARGIN =
    (DISP_WIDTH - SELECT_CARD_W) / 2;

static struct
{
    int32_t active_offset;
    int32_t scroll_offset;
    int32_t fill_w;
    button_id_t fill_btn;
    bool was_pressed;
    bool ignore_release;
} sel;

static inline int32_t select_card_x(uint8_t idx)
{
    return (SELECT_CARD_W + SELECT_CARD_GAP) * idx;
}

static void select_retarget(bool instant)
{
    int32_t active = select_card_x((uint8_t)state.selected_track);
    int32_t scroll = SELECT_SCROLL_MARGIN - active;

    if (instant)
    {
        sel.active_offset = active;
        sel.scroll_offset = scroll;
        return;
    }

    anim_to(&sel.active_offset, active, 150, ANIM_EASE_INOUT_QUAD, NULL, NULL);
    anim_to(&sel.scroll_offset, scroll, 150, ANIM_EASE_INOUT_QUAD, NULL, NULL);
}

static void select_enter(void)
{
    memset(&sel, 0, sizeof(sel));
    select_retarget(true);
}

static void select_draw_stars(elm_t *parent, vec2_t pos, uint8_t filled,
                              uint8_t total)
{
    u8g2_t *u8g2 = parent->u8g2;
    for (uint8_t i = 0; i < total; i++)
    {
        int16_t x = pos.x + i * SELECT_STAR_W;
        int16_t y = pos.y;
        if (i < filled)
            u8g2_DrawDisc(u8g2, parent->pos.x + x + 2, parent->pos.y + y + 2, 1,
                          U8G2_DRAW_ALL);
        else
            u8g2_DrawCircle(u8g2, parent->pos.x + x + 2, parent->pos.y + y + 2, 1,
                            U8G2_DRAW_ALL);
    }
}

static void select_draw_centered_trimmed(u8g2_t *u8g2, int16_t y, const char *txt,
                                         int16_t max_w)
{
    char buf[48];
    size_t n = strlen(txt);
    if (n >= sizeof(buf))
        n = sizeof(buf) - 1;
    memcpy(buf, txt, n);
    buf[n] = '\0';

    while (u8g2_GetStrWidth(u8g2, buf) > max_w && n > 0)
    {
        if (n > 3)
        {
            n--;
            buf[n - 2] = '.';
            buf[n - 1] = '.';
            buf[n] = '.';
            buf[n + 1] = '\0';
        }
        else
        {
            n--;
            buf[n] = '\0';
        }
    }

    uint16_t w = u8g2_GetStrWidth(u8g2, buf);
    int16_t x = (DISP_WIDTH - (int16_t)w) / 2;
    u8g2_DrawStr(u8g2, x, y, buf);
}

static void select_tick(void)
{
    // handle hold-to-select
    button_id_t btn = engine_button_get_pressed_first();
    if (btn != BUTTON_NONE)
    {
        float held = engine_button_held_ratio(btn);
        sel.ignore_release = held > 0.f;
        if (held > 0.f)
        {
            anim_cancel(&sel.fill_w, false);
            float eased = held * held * (3.f - 2.f * held); // smoothstep
            sel.fill_w = (int32_t)((SELECT_HOLD_BOX_W - 2) * eased);
            sel.fill_btn = btn;
        }
        if (held >= 1.f)
        {
            beatline_game_select_track(&state, state.selected_track);
            beatline_game_start_countdown(&state);
            engine_buttons_reset();
            return;
        }
    }

    // handle scroll on release
    if (BUTTON_KEYUP(BUTTON_LEFT))
    {
        anim_to(&sel.fill_w, 0, 100, ANIM_EASE_OUT_CUBIC, NULL, NULL);
        if (!sel.ignore_release)
        {
            state.selected_track--;
            if (state.selected_track < 0)
                state.selected_track = BEATLINE_TRACK_COUNT - 1;
            select_retarget(false);
            g_engine.led_colors[LED_L] = rgba(30, 30, 30, 255);
        }
    }
    if (BUTTON_KEYUP(BUTTON_RIGHT))
    {
        anim_to(&sel.fill_w, 0, 100, ANIM_EASE_OUT_CUBIC, NULL, NULL);
        if (!sel.ignore_release)
        {
            state.selected_track++;
            if (state.selected_track >= BEATLINE_TRACK_COUNT)
                state.selected_track = 0;
            select_retarget(false);
            g_engine.led_colors[LED_R] = rgba(30, 30, 30, 255);
        }
    }
}

static void select_frame(void)
{
    u8g2_t *u8g2 = platform_display_get_u8g2();
    elm_t root = elm_root(u8g2, VEC2_Z);

    u8g2_SetDrawColor(u8g2, 1);
    char idx_buf[12];
    snprintf(idx_buf, sizeof(idx_buf), "%u/%u", (unsigned)(state.selected_track + 1),
             (unsigned)BEATLINE_TRACK_COUNT);
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    elm_str(&root, vec2(DISP_WIDTH - u8g2_GetStrWidth(u8g2, idx_buf) - 2, 8), idx_buf);

    const beatline_chart_t *t = &beatline_tracks[state.selected_track];
    int16_t card_x = 1;
    int16_t card_y = SELECT_CARD_Y;

    // Full-screen song panel.
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawRFrame(u8g2, card_x, card_y, SELECT_CARD_W, SELECT_CARD_H, 3);

    // compact hold-to-start box near the bottom of the panel
    int16_t hold_x = (DISP_WIDTH - SELECT_HOLD_BOX_W) / 2;
    int16_t hold_y = DISP_HEIGHT - SELECT_HOLD_BOX_H - 4;
    u8g2_DrawRFrame(u8g2, hold_x, hold_y, SELECT_HOLD_BOX_W, SELECT_HOLD_BOX_H, 2);

    // hold fill
    if (sel.fill_w > 0)
    {
        u8g2_SetDrawColor(u8g2, 2); // XOR
        if (sel.fill_btn == BUTTON_RIGHT)
            u8g2_DrawBox(u8g2, hold_x + SELECT_HOLD_BOX_W - 1 - sel.fill_w, hold_y + 1,
                         sel.fill_w, SELECT_HOLD_BOX_H - 2);
        else
            u8g2_DrawBox(u8g2, hold_x + 1, hold_y + 1, sel.fill_w, SELECT_HOLD_BOX_H - 2);
        u8g2_SetDrawColor(u8g2, 1);
    }

    // title + metadata
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    select_draw_centered_trimmed(u8g2, 20, t->title, DISP_WIDTH - 8);

    char bpm_buf[16];
    snprintf(bpm_buf, sizeof(bpm_buf), "%u BPM", t->bpm);
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    select_draw_centered_trimmed(u8g2, 33, bpm_buf, DISP_WIDTH - 8);

    int16_t star_x = (DISP_WIDTH - (5 * SELECT_STAR_W)) / 2;
    select_draw_stars(&root, vec2(star_x, 37), t->difficulty, 5);

    u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
    select_draw_centered_trimmed(u8g2, hold_y + 8, "HOLD TO START", SELECT_HOLD_BOX_W - 6);

    // LED ramp during hold
    if (sel.fill_w > 0)
    {
        float ratio = (float)sel.fill_w / (float)(SELECT_HOLD_BOX_W - 2);
        uint8_t brightness = (uint8_t)(ratio * 255);
        if (sel.fill_btn == BUTTON_LEFT)
            g_engine.led_colors[LED_L] = rgba(brightness, brightness, brightness, 255);
        else
            g_engine.led_colors[LED_R] = rgba(brightness, brightness, brightness, 255);
    }
}

// ============================================================
// Countdown Screen
// ============================================================

static void countdown_frame(void)
{
    u8g2_t *u8g2 = platform_display_get_u8g2();
    elm_t root = elm_root(u8g2, VEC2_Z);

    u8g2_SetDrawColor(u8g2, 1);

    // track title at top
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    uint16_t tw = u8g2_GetStrWidth(u8g2, state.chart->title);
    elm_str(&root, vec2((DISP_WIDTH - tw) / 2, 10), state.chart->title);

    // big countdown number
    char num[2] = {(char)('0' + state.countdown_beat), '\0'};
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    uint16_t nw = u8g2_GetStrWidth(u8g2, num);
    elm_str(&root, vec2((DISP_WIDTH - nw) / 2, 40), num);

    // LED flash on each beat transition
    uint32_t beat_interval = 60000 / state.chart->bpm;
    uint32_t since_beat = g_engine.tick -
                          (state.countdown_next_tick - beat_interval);
    if (since_beat < 100)
    {
        // flash — brighter on final beat
        uint8_t bri = (state.countdown_beat == 1) ? 255 : 120;
        g_engine.led_colors[LED_L] = rgba(bri, bri, bri, 255);
        g_engine.led_colors[LED_R] = rgba(bri, bri, bri, 255);
    }
}

// ============================================================
// Gameplay Screen
// ============================================================

static float scroll_px_per_tick(void)
{
    // scroll_speed is px/sec, tick rate is 1000 Hz
    return state.chart->scroll_speed / 1000.f;
}

#define NOTE_W 6
#define NOTE_H 5
#define NOTE_HOLD_H 4
#define NOTE_APPROACH_TICKS 200
#define BEATLINE_HIT_LINE_LEFT_X (BEATLINE_HIT_ZONE_W * 2 - 2)
#define BEATLINE_HIT_LINE_RIGHT_X (DISP_WIDTH - BEATLINE_HIT_ZONE_W * 2 + 1)

#define BEATLINE_TOP_TRACK_Y 2
#define BEATLINE_TOP_TRACK_H 26
#define BEATLINE_BOTTOM_TRACK_Y 37
#define BEATLINE_BOTTOM_TRACK_H 25

static void draw_dashed_vline(u8g2_t *u8g2, int16_t x, int16_t y, int16_t h)
{
    for (int16_t yy = y; yy < y + h; yy += 4)
    {
        int16_t seg = (int16_t)(y + h - yy);
        if (seg > 2)
            seg = 2;
        if (seg > 0)
            u8g2_DrawVLine(u8g2, x, yy, seg);
    }
}

static void play_draw_hit_zones(elm_t *root)
{
    u8g2_t *u8g2 = root->u8g2;
    u8g2_SetDrawColor(u8g2, 1);
    // Double-thick hit lines for stronger visual anchor.
    u8g2_DrawVLine(u8g2, BEATLINE_HIT_LINE_LEFT_X, BEATLINE_TOP_TRACK_Y, BEATLINE_TOP_TRACK_H);
    u8g2_DrawVLine(u8g2, BEATLINE_HIT_LINE_LEFT_X + 1, BEATLINE_TOP_TRACK_Y, BEATLINE_TOP_TRACK_H);
    u8g2_DrawVLine(u8g2, BEATLINE_HIT_LINE_RIGHT_X, BEATLINE_BOTTOM_TRACK_Y, BEATLINE_BOTTOM_TRACK_H);
    u8g2_DrawVLine(u8g2, BEATLINE_HIT_LINE_RIGHT_X + 1, BEATLINE_BOTTOM_TRACK_Y, BEATLINE_BOTTOM_TRACK_H);
}

static void play_draw_lane_divider(elm_t *root)
{
    u8g2_t *u8g2 = root->u8g2;
    u8g2_SetDrawColor(u8g2, 1);
    // Dashed divider between the two lanes.
    for (int16_t x = 0; x < DISP_WIDTH; x += 4)
    {
        u8g2_DrawPixel(u8g2, x, BEATLINE_LANE_DIVIDER_Y);
        u8g2_DrawPixel(u8g2, x + 1, BEATLINE_LANE_DIVIDER_Y);
    }
}

static void play_draw_grid(elm_t *root)
{
    u8g2_t *u8g2 = root->u8g2;
    u8g2_SetDrawColor(u8g2, 1);
    float px_t = scroll_px_per_tick();
    uint32_t game_t = beatline_game_time(&state);
    uint32_t beat_ticks = (state.chart->bpm > 0) ? (60000u / state.chart->bpm) : 500u;
    if (beat_ticks == 0)
        beat_ticks = 1;

    float beat_px = (float)beat_ticks * px_t;
    if (beat_px < 6.0f)
        beat_px = 6.0f;

    float frac = (float)(game_t % beat_ticks) / (float)beat_ticks;
    float top_anchor = (float)BEATLINE_HIT_LINE_LEFT_X - frac * beat_px;
    float bot_anchor = (float)BEATLINE_HIT_LINE_RIGHT_X + frac * beat_px;

    // Quarter-note guides for the top lane (moving toward the left hit line).
    for (float x = top_anchor; x < DISP_WIDTH; x += beat_px)
    {
        int16_t xi = (int16_t)x;
        if (xi <= BEATLINE_HIT_ZONE_W || xi >= DISP_WIDTH - BEATLINE_HIT_ZONE_W)
            continue;
        draw_dashed_vline(u8g2, xi, BEATLINE_TOP_TRACK_Y, BEATLINE_TOP_TRACK_H);
    }
    for (float x = top_anchor - beat_px; x > 0.0f; x -= beat_px)
    {
        int16_t xi = (int16_t)x;
        if (xi <= BEATLINE_HIT_ZONE_W || xi >= DISP_WIDTH - BEATLINE_HIT_ZONE_W)
            continue;
        draw_dashed_vline(u8g2, xi, BEATLINE_TOP_TRACK_Y, BEATLINE_TOP_TRACK_H);
    }

    // Quarter-note guides for the bottom lane (moving toward the right hit line).
    for (float x = bot_anchor; x > 0.0f; x -= beat_px)
    {
        int16_t xi = (int16_t)x;
        if (xi <= BEATLINE_HIT_ZONE_W || xi >= DISP_WIDTH - BEATLINE_HIT_ZONE_W)
            continue;
        draw_dashed_vline(u8g2, xi, BEATLINE_BOTTOM_TRACK_Y, BEATLINE_BOTTOM_TRACK_H);
    }
    for (float x = bot_anchor + beat_px; x < DISP_WIDTH; x += beat_px)
    {
        int16_t xi = (int16_t)x;
        if (xi <= BEATLINE_HIT_ZONE_W || xi >= DISP_WIDTH - BEATLINE_HIT_ZONE_W)
            continue;
        draw_dashed_vline(u8g2, xi, BEATLINE_BOTTOM_TRACK_Y, BEATLINE_BOTTOM_TRACK_H);
    }
}

static void play_draw_notes(elm_t *root)
{
    u8g2_t *u8g2 = root->u8g2;
    uint32_t game_time = beatline_game_time(&state);
    float px_t = scroll_px_per_tick();

    for (uint16_t i = 0; i < state.generated_note_count; i++)
    {
        if (state.note_grades[i] != BEATLINE_NOTE_UNJUDGED)
        {
            // hold notes still render tail while being held
            if (state.generated_notes[i].type == BEATLINE_NOTE_HOLD)
            {
                uint8_t lane = state.generated_notes[i].lane;
                if (state.hold_state[lane].holding &&
                    state.hold_state[lane].note_idx == i)
                {
                    // draw remaining hold tail — fall through below
                }
                else
                {
                    continue;
                }
            }
            else
            {
                continue;
            }
        }

        const beatline_note_t *n = &state.generated_notes[i];
        int32_t time_remaining = (int32_t)n->hit_tick - (int32_t)game_time;
        int32_t travel_px = (int32_t)(time_remaining * px_t);

        // compute note screen x
        int16_t note_x;
        int16_t note_y;

        if (n->lane == BEATLINE_LANE_LEFT)
        {
            // top lane: perfect when the note's center reaches the hit line
            note_x = BEATLINE_HIT_LINE_LEFT_X - NOTE_W / 2 + travel_px;
            note_y = BEATLINE_TOP_TRACK_Y + (BEATLINE_TOP_TRACK_H / 2) - NOTE_H / 2;
        }
        else
        {
            // bottom lane: perfect when the note's center reaches the hit line
            note_x = BEATLINE_HIT_LINE_RIGHT_X - NOTE_W / 2 - travel_px;
            note_y = BEATLINE_BOTTOM_TRACK_Y + (BEATLINE_BOTTOM_TRACK_H / 2) - NOTE_H / 2;
        }

        int32_t hold_px = NOTE_W;
        if (n->type == BEATLINE_NOTE_HOLD)
        {
            hold_px = (int32_t)(n->hold_duration * px_t);
            if (hold_px < NOTE_W)
                hold_px = NOTE_W;

            // Cull using full hold body bounds, not just the head.
            int32_t body_min_x;
            int32_t body_max_x;
            if (n->lane == BEATLINE_LANE_LEFT)
            {
                body_min_x = note_x;
                body_max_x = note_x + hold_px;
            }
            else
            {
                body_min_x = note_x - hold_px + NOTE_W;
                body_max_x = note_x + NOTE_W;
            }
            if (body_max_x < 0 || body_min_x > DISP_WIDTH)
                continue;
        }
        else
        {
            // tap note cull
            if (note_x < -NOTE_W || note_x > DISP_WIDTH + NOTE_W)
                continue;
        }

        if (n->type == BEATLINE_NOTE_HOLD)
        {
            if (n->lane == BEATLINE_LANE_LEFT)
            {
                // tail extends to the right from note head
                u8g2_SetDrawColor(u8g2, 1);
                u8g2_DrawBox(u8g2, note_x, note_y + 1, hold_px, NOTE_HOLD_H - 1);
                // head
                u8g2_DrawBox(u8g2, note_x, note_y, NOTE_W, NOTE_H);
                // end cap
                u8g2_DrawFrame(u8g2, note_x + hold_px - 2, note_y, 3, NOTE_H);
            }
            else
            {
                // tail extends to the left from note head
                u8g2_SetDrawColor(u8g2, 1);
                u8g2_DrawBox(u8g2, note_x - hold_px + NOTE_W, note_y + 1, hold_px, NOTE_HOLD_H - 1);
                // head
                u8g2_DrawFrame(u8g2, note_x, note_y, NOTE_W, NOTE_H);
                // end cap
                u8g2_DrawFrame(u8g2, note_x - hold_px + NOTE_W, note_y, 3, NOTE_H);
            }
        }
        else
        {
            // tap note
            u8g2_SetDrawColor(u8g2, 1);
            u8g2_DrawBox(u8g2, note_x, note_y, NOTE_W, NOTE_H); // filled
        }
    }
}

static void play_draw_feedback(elm_t *root)
{
    u8g2_t *u8g2 = root->u8g2;

    for (uint8_t lane = 0; lane < 2; lane++)
    {
        if (state.feedback[lane].until_tick == 0)
            continue;

        u8g2_SetDrawColor(u8g2, 1);
        u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);

        const char *txt = beatline_grade_str(state.feedback[lane].grade);
        if (state.feedback[lane].grade == BEATLINE_GRADE_BAD)
            txt = state.feedback[lane].timing_late ? "LATE" : "EARLY";
        uint16_t tw = u8g2_GetStrWidth(u8g2, txt);

        int16_t fb_y;
        if (lane == BEATLINE_LANE_LEFT)
            fb_y = BEATLINE_TOP_TRACK_Y + (BEATLINE_TOP_TRACK_H / 2) - 2;
        else
            fb_y = BEATLINE_BOTTOM_TRACK_Y + (BEATLINE_BOTTOM_TRACK_H / 2) - 2;

        u8g2_DrawStr(u8g2, (DISP_WIDTH - tw) / 2, fb_y, txt);
    }
}

static void play_set_leds(void)
{
    for (uint8_t lane = 0; lane < 2; lane++)
    {
        uint8_t led = (lane == BEATLINE_LANE_LEFT) ? LED_L : LED_R;

        if (state.feedback[lane].until_tick == 0)
            continue;

        uint32_t remaining = state.feedback[lane].until_tick - g_engine.tick;
        uint32_t elapsed = BEATLINE_FEEDBACK_DURATION - remaining;
        if (elapsed >= BEATLINE_LED_DURATION)
            continue;

        switch (state.feedback[lane].grade)
        {
        case BEATLINE_GRADE_PERFECT:
            g_engine.led_colors[led] = rgba(200, 255, 255, 255); // cyan-white
            break;
        case BEATLINE_GRADE_GOOD:
            g_engine.led_colors[led] = rgba(0, 255, 0, 255);
            break;
        case BEATLINE_GRADE_BAD:
            g_engine.led_colors[led] = rgba(255, 180, 0, 255);
            break;
        case BEATLINE_GRADE_MISS:
            g_engine.led_colors[led] = rgba(255, 0, 0, 255);
            break;
        }
    }

    // hold sustain: pulse LED while holding
    for (uint8_t lane = 0; lane < 2; lane++)
    {
        if (!state.hold_state[lane].holding)
            continue;

        uint8_t led = (lane == BEATLINE_LANE_LEFT) ? LED_L : LED_R;
        // slow pulse: sine-like using tick
        uint8_t bri = 80 + (uint8_t)(80.f * led_sine_pulse(g_engine.tick, 0.006f));
        g_engine.led_colors[led] = rgba(0, bri, bri, 255);
    }
}

static void play_frame(void)
{
    u8g2_t *u8g2 = platform_display_get_u8g2();
    elm_t root = elm_root(u8g2, VEC2_Z);

    u8g2_SetDrawColor(u8g2, 1);

    play_draw_grid(&root);
    play_draw_lane_divider(&root);
    play_draw_hit_zones(&root);
    play_draw_notes(&root);
    play_draw_feedback(&root);
    play_set_leds();
}

// ============================================================
// Results Screen
// ============================================================

static void results_frame(void)
{
    u8g2_t *u8g2 = platform_display_get_u8g2();
    elm_t root = elm_root(u8g2, VEC2_Z);

    u8g2_SetDrawColor(u8g2, 1);

    // title and rank
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    elm_str(&root, vec2(0, 10), "RESULTS");
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    elm_str(&root, vec2(0, 18), state.chart->title);

    const char *rank = beatline_rank_str(beatline_game_rank(&state));
    u8g2_SetFont(u8g2, u8g2_font_7x14_mr);
    uint16_t rw = u8g2_GetStrWidth(u8g2, rank);
    elm_str(&root, vec2(70 - rw, 16), rank);

    // stat column (left)
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    char buf[32];

    snprintf(buf, sizeof(buf), "S:%lu", (unsigned long)state.score);
    elm_str(&root, vec2(0, 29), buf);

    snprintf(buf, sizeof(buf), "MAX:%u", state.max_combo);
    elm_str(&root, vec2(0, 38), buf);

    snprintf(buf, sizeof(buf), "P:%u G:%u",
             state.grade_counts[BEATLINE_GRADE_PERFECT],
             state.grade_counts[BEATLINE_GRADE_GOOD]);
    elm_str(&root, vec2(0, 47), buf);

    snprintf(buf, sizeof(buf), "B:%u M:%u",
             state.grade_counts[BEATLINE_GRADE_BAD],
             state.grade_counts[BEATLINE_GRADE_MISS]);
    elm_str(&root, vec2(0, 56), buf);

    // leaderboard QR (right)
    if (state.qr_ready)
    {
        elm_qrcode(&root, vec2(127 - 3, 3), ELM_ALIGN_TOP_RIGHT, state.qr_code, 2, 1);
    }
    else
    {
        u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
        elm_str(&root, vec2(84, 34), "QR ERR");
    }

    bool pressed = false;
    elm_btn(&root, vec2(126, 62), "NEW RUN?", ELM_ALIGN_BOTTOM_RIGHT, &pressed);
    if (pressed)
    {
        beatline_game_init(&state);
        select_enter();
    }
}

// ============================================================
// App Lifecycle
// ============================================================

static void enter(void)
{
    beatline_game_init(&state);
    select_enter();
}

static void tick(void)
{
    if (state.screen == BEATLINE_SCREEN_SELECT)
        select_tick();
    beatline_game_tick(&state);
}

static void frame(void)
{
    switch (state.screen)
    {
    case BEATLINE_SCREEN_SELECT:
        select_frame();
        break;
    case BEATLINE_SCREEN_COUNTDOWN:
        countdown_frame();
        break;
    case BEATLINE_SCREEN_PLAY:
        play_frame();
        break;
    case BEATLINE_SCREEN_RESULTS:
        results_frame();
        break;
    }
}

static void pause(void)
{
    if (state.screen == BEATLINE_SCREEN_PLAY)
        audio_song_player_pause(&state.song_player);
}

static void resume(void)
{
    if (state.screen == BEATLINE_SCREEN_PLAY)
        audio_song_player_resume(&state.song_player, g_engine.tick);
}

static void leave(void)
{
    audio_song_player_stop(&state.song_player, true);
}

app_t app_beatline = {
    .name = "beatline",
    .enter = enter,
    .tick = tick,
    .frame = frame,
    .pause = pause,
    .resume = resume,
    .leave = leave,
};
