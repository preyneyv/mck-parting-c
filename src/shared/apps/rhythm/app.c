#include <stdio.h>
#include <string.h>

#include <shared/apps/apps.h>
#include <shared/engine.h>
#include <shared/utils/elm.h>
#include <shared/utils/q1x31.h>

#include "tracker.h"

#define LANE_LEFT 0
#define LANE_RIGHT 1
#define SFX_VOICE_LEFT 6
#define SFX_VOICE_RIGHT 7

typedef struct
{
    uint32_t time_ms;
    uint8_t lane;
} note_t;

// Embedded, human-modifiable chart (time in ms)
static const note_t chart[] = {
    {500, LANE_LEFT},
    {750, LANE_RIGHT},
    {1000, LANE_LEFT},
    {1250, LANE_RIGHT},
    {1500, LANE_LEFT},
    {1750, LANE_RIGHT},
    {2000, LANE_LEFT},
    {2300, LANE_RIGHT},
    {2600, LANE_LEFT},
    {2900, LANE_RIGHT},
    {3200, LANE_LEFT},
    {3500, LANE_RIGHT},
    {3800, LANE_LEFT},
    {4100, LANE_RIGHT},
    {4400, LANE_LEFT},
    {4700, LANE_RIGHT},
    {5000, LANE_LEFT},
    {5300, LANE_RIGHT},
    {5600, LANE_LEFT},
    {5900, LANE_RIGHT},
    {6200, LANE_LEFT},
    {6500, LANE_RIGHT},
    {6800, LANE_LEFT},
    {7100, LANE_RIGHT},
    {7400, LANE_LEFT},
    {7700, LANE_RIGHT},
    {8000, LANE_LEFT},
    {8300, LANE_RIGHT},
    {8600, LANE_LEFT},
    {8900, LANE_RIGHT},
    {9200, LANE_LEFT},
    {9500, LANE_RIGHT},
};

#define CHART_LEN (sizeof(chart) / sizeof(chart[0]))

enum
{
    NOTE_C5 = 72,
    NOTE_D5 = 74,
    NOTE_E5 = 76,
    NOTE_G5 = 79,
};

static const tracker_note_t music[] = {
    {500, 0, NOTE_C5, 90, 120},
    {750, 1, NOTE_E5, 90, 120},
    {1000, 0, NOTE_G5, 90, 120},
    {1250, 1, NOTE_E5, 90, 120},
    {1500, 0, NOTE_D5, 90, 120},
    {1750, 1, NOTE_G5, 90, 120},
    {2000, 0, NOTE_C5, 90, 160},
    {2300, 1, NOTE_E5, 90, 160},
    {2600, 0, NOTE_G5, 90, 160},
    {2900, 1, NOTE_E5, 90, 160},
    {3200, 0, NOTE_D5, 90, 160},
    {3500, 1, NOTE_G5, 90, 160},
    {3800, 0, NOTE_C5, 90, 180},
    {4100, 1, NOTE_E5, 90, 180},
    {4400, 0, NOTE_G5, 90, 180},
    {4700, 1, NOTE_E5, 90, 180},
    {5000, 0, NOTE_D5, 90, 180},
    {5300, 1, NOTE_G5, 90, 180},
    {5600, 0, NOTE_C5, 90, 200},
    {5900, 1, NOTE_E5, 90, 200},
    {6200, 0, NOTE_G5, 90, 200},
    {6500, 1, NOTE_E5, 90, 200},
    {6800, 0, NOTE_D5, 90, 200},
    {7100, 1, NOTE_G5, 90, 200},
    {7400, 0, NOTE_C5, 90, 220},
    {7700, 1, NOTE_E5, 90, 220},
    {8000, 0, NOTE_G5, 90, 220},
    {8300, 1, NOTE_E5, 90, 220},
    {8600, 0, NOTE_D5, 90, 220},
    {8900, 1, NOTE_G5, 90, 220},
    {9200, 0, NOTE_C5, 90, 240},
    {9500, 1, NOTE_E5, 90, 240},
};

#define MUSIC_LEN (sizeof(music) / sizeof(music[0]))

static const int16_t LANE_X[2] = {32, 96};
static const int16_t HIT_LINE_Y = 52;
static const int16_t NOTE_W = 6;
static const int16_t NOTE_H = 4;

static const uint16_t HIT_WINDOW_MS = 200;
static const uint16_t HIT_FEEDBACK_MS = 150;
static const uint16_t NOTE_OFF_MS = 90;

// 80 px / sec -> 0.08 px/ms
static const int32_t SCROLL_PX_PER_MS_NUM = 80;
static const int32_t SCROLL_PX_PER_MS_DEN = 1000;

typedef struct
{
    uint32_t start_ms;
    uint16_t next_miss_index;
    uint32_t score;
    uint16_t combo;
    uint16_t max_combo;
    uint16_t hits;
    uint16_t misses;
    bool finished;
    int8_t last_result; // -1 miss, 0 none, 1 hit
    uint32_t last_result_until;
    uint32_t note_off_at[2];
    bool note_hit[CHART_LEN];
} state_t;

static state_t state;
static tracker_t tracker;

static inline uint32_t now_ms()
{
    return g_engine.tick; // tick rate is 1000 Hz
}

static void reset_state()
{
    memset(&state, 0, sizeof(state));
    state.start_ms = now_ms();
}

static void audio_note_on(uint8_t lane)
{
    uint8_t voice = lane == LANE_LEFT ? SFX_VOICE_LEFT : SFX_VOICE_RIGHT;
    uint16_t note_num = lane == LANE_LEFT ? note("C5") : note("E5");
    audio_synth_enqueue(&g_engine.synth,
                        &(audio_synth_message_t){
                            .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                            .data.note_on = {.voice = voice,
                                             .note_number = note_num,
                                             .velocity = 100},
                        });
    state.note_off_at[lane] = now_ms() + NOTE_OFF_MS;
}

static void audio_note_off(uint8_t lane)
{
    uint8_t voice = lane == LANE_LEFT ? SFX_VOICE_LEFT : SFX_VOICE_RIGHT;
    audio_synth_enqueue(&g_engine.synth,
                        &(audio_synth_message_t){
                            .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                            .data.note_off = {.voice = voice},
                        });
}

static void configure_synth()
{
    audio_synth_operator_config_t config = audio_synth_operator_config_default;
    config.level = q1x15_f(.5f);
    config.env = (audio_synth_env_config_t){
        .a = 2,
        .d = 0,
        .s = q1x31_f(1.0f),
        .r = 5,
    };

    audio_synth_operator_set_config(&g_engine.synth.voices[SFX_VOICE_LEFT].ops[0], config);
    audio_synth_operator_set_config(&g_engine.synth.voices[SFX_VOICE_RIGHT].ops[0], config);
}

static void enter()
{
    tracker_voice_config_t voice_cfg = {
        .ops = {
            [0] = {
                .freq_mult = 1,
                .level = q1x15_f(.6f),
                .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
                .env = {
                    .a = 2,
                    .d = 8,
                    .s = q1x31_f(0.8f),
                    .r = 10,
                },
            },
        },
    };

    tracker_chart_t music_chart = {
        .notes = music,
        .count = MUSIC_LEN,
    };

    configure_synth();
    reset_state();

    tracker_init(&tracker, &g_engine.synth, &music_chart);
    tracker_set_voice_config(&tracker, 0, &voice_cfg);
    tracker_set_voice_config(&tracker, 1, &voice_cfg);
    tracker_start(&tracker, state.start_ms);
}

static void register_hit(uint8_t lane)
{
    state.hits++;
    state.combo++;
    if (state.combo > state.max_combo)
    {
        state.max_combo = state.combo;
    }
    state.score += 100 + (state.combo / 5) * 20;
    state.last_result = 1;
    state.last_result_until = now_ms() + HIT_FEEDBACK_MS;
    audio_note_on(lane);
}

static void register_miss()
{
    state.misses++;
    state.combo = 0;
    state.last_result = -1;
    state.last_result_until = now_ms() + HIT_FEEDBACK_MS;
}

static bool try_hit_lane(uint8_t lane, uint32_t current_ms)
{
    for (uint16_t i = state.next_miss_index; i < CHART_LEN; i++)
    {
        if (state.note_hit[i])
            continue;
        int32_t dt = (int32_t)chart[i].time_ms - (int32_t)current_ms;
        if (dt > (int32_t)HIT_WINDOW_MS)
        {
            break; // future notes too far
        }
        if (dt < -(int32_t)HIT_WINDOW_MS)
        {
            continue; // too late, miss logic will handle
        }
        if (chart[i].lane != lane)
        {
            continue;
        }

        state.note_hit[i] = true;
        register_hit(lane);
        return true;
    }
    register_miss();
    return false;
}

static void process_misses(uint32_t current_ms)
{
    while (state.next_miss_index < CHART_LEN)
    {
        uint16_t i = state.next_miss_index;
        if (state.note_hit[i])
        {
            state.next_miss_index++;
            continue;
        }
        if (chart[i].time_ms + HIT_WINDOW_MS < current_ms)
        {
            state.note_hit[i] = true;
            register_miss();
            state.next_miss_index++;
            continue;
        }
        break;
    }
}

static void update_note_off(uint32_t current_ms)
{
    for (uint8_t lane = 0; lane < 2; lane++)
    {
        if (state.note_off_at[lane] != 0 && current_ms >= state.note_off_at[lane])
        {
            audio_note_off(lane);
            state.note_off_at[lane] = 0;
        }
    }
}

static void tick_game()
{
    uint32_t current_ms = now_ms();

    tracker_tick(&tracker, current_ms);

    if (BUTTON_KEYDOWN(BUTTON_LEFT))
    {
        try_hit_lane(LANE_LEFT, current_ms);
    }
    if (BUTTON_KEYDOWN(BUTTON_RIGHT))
    {
        try_hit_lane(LANE_RIGHT, current_ms);
    }

    process_misses(current_ms);
    update_note_off(current_ms);

    if (state.last_result_until != 0 && current_ms > state.last_result_until)
    {
        state.last_result = 0;
        state.last_result_until = 0;
    }

    if (state.next_miss_index >= CHART_LEN)
    {
        uint32_t end_ms = chart[CHART_LEN - 1].time_ms + 1500;
        if (current_ms > end_ms)
        {
            state.finished = true;
            engine_buttons_reset();
            tracker_stop(&tracker);
        }
    }
}

static void tick()
{
    if (!state.finished)
    {
        tick_game();
    }
}

static void draw_lanes(u8g2_t *u8g2)
{
    u8g2_SetDrawColor(u8g2, 1);
    for (uint8_t i = 0; i < 2; i++)
    {
        u8g2_DrawVLine(u8g2, LANE_X[i], 8, DISP_HEIGHT - 12);
    }
    u8g2_DrawHLine(u8g2, 0, HIT_LINE_Y, DISP_WIDTH);
}

static void draw_notes(u8g2_t *u8g2, uint32_t current_ms)
{
    for (uint16_t i = 0; i < CHART_LEN; i++)
    {
        if (state.note_hit[i])
        {
            continue;
        }
        int32_t dt = (int32_t)chart[i].time_ms - (int32_t)current_ms;
        int32_t y = HIT_LINE_Y - (dt * SCROLL_PX_PER_MS_NUM / SCROLL_PX_PER_MS_DEN);
        if (y < 0 || y > (DISP_HEIGHT - NOTE_H))
        {
            continue;
        }

        int32_t x = LANE_X[chart[i].lane] - NOTE_W / 2;
        u8g2_DrawBox(u8g2, x, (uint8_t)y, NOTE_W, NOTE_H);
    }
}

static void draw_hud(u8g2_t *u8g2)
{
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);

    char buf[32];
    snprintf(buf, sizeof(buf), "S:%lu", state.score);
    u8g2_DrawStr(u8g2, 0, 6, buf);

    snprintf(buf, sizeof(buf), "C:%u", state.combo);
    u8g2_DrawStr(u8g2, 64, 6, buf);
}

static void draw_feedback(u8g2_t *u8g2)
{
    if (state.last_result == 0)
    {
        return;
    }
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    const char *txt = state.last_result > 0 ? "OK" : "MISS";
    uint16_t w = u8g2_GetStrWidth(u8g2, txt);
    u8g2_DrawStr(u8g2, (DISP_WIDTH - w) / 2, 20, txt);
}

static void frame_game()
{
    u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);
    uint32_t current_ms = now_ms();

    draw_lanes(u8g2);
    draw_notes(u8g2, current_ms);
    draw_hud(u8g2);
    draw_feedback(u8g2);

    if (state.last_result > 0)
    {
        leds_set_all(&g_engine.leds, (color_t){.hex = 0x00ff00});
    }
    else if (state.last_result < 0)
    {
        leds_set_all(&g_engine.leds, (color_t){.hex = 0xff0000});
    }
    else
    {
        leds_set_all(&g_engine.leds, (color_t){.hex = 0x000000});
    }
}

static void frame_results()
{
    u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);
    elm_t root = elm_root(u8g2, VEC2_Z);

    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    elm_str(&root, vec2(0, 12), "RESULTS");

    char buf[32];
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    snprintf(buf, sizeof(buf), "Score: %lu", state.score);
    elm_str(&root, vec2(0, 28), buf);
    snprintf(buf, sizeof(buf), "Hits: %u", state.hits);
    elm_str(&root, vec2(0, 38), buf);
    snprintf(buf, sizeof(buf), "Miss: %u", state.misses);
    elm_str(&root, vec2(0, 48), buf);
    snprintf(buf, sizeof(buf), "Max: %u", state.max_combo);
    elm_str(&root, vec2(0, 58), buf);

    bool pressed = false;
    elm_btn(&root, vec2(126, 62), "RETRY", ELM_ALIGN_BOTTOM_RIGHT, &pressed);
    if (pressed)
    {
        reset_state();
    }

    leds_set_all(&g_engine.leds, (color_t){.hex = 0x0000ff});
}

static void frame()
{
    if (state.finished)
    {
        frame_results();
    }
    else
    {
        frame_game();
    }
}

app_t app_rhythm = {
    .name = "rhythm",
    .enter = enter,
    .tick = tick,
    .frame = frame,
};
