#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <shared/audio/synth.h>

typedef struct
{
    audio_synth_operator_config_t ops[AUDIO_SYNTH_OPERATOR_COUNT];
} tracker_voice_config_t;

typedef struct
{
    uint32_t time_ms;
    uint8_t voice;
    uint16_t note;
    uint8_t velocity;
    uint16_t duration_ms;
} tracker_note_t;

typedef struct
{
    const tracker_note_t *notes;
    uint16_t count;
} tracker_chart_t;

typedef struct
{
    uint32_t start_ms;
    uint16_t next_index;
    struct
    {
        bool active;
        uint32_t off_ms;
    } voice_state[AUDIO_SYNTH_VOICE_COUNT];
} tracker_state_t;

typedef struct
{
    audio_synth_t *synth;
    tracker_chart_t chart;
    tracker_state_t state;
} tracker_t;

void tracker_init(tracker_t *tracker, audio_synth_t *synth,
                  const tracker_chart_t *chart);
void tracker_set_chart(tracker_t *tracker, const tracker_chart_t *chart);
void tracker_set_voice_config(tracker_t *tracker, uint8_t voice,
                              const tracker_voice_config_t *cfg);
void tracker_start(tracker_t *tracker, uint32_t start_ms);
void tracker_stop(tracker_t *tracker);
void tracker_tick(tracker_t *tracker, uint32_t now_ms);
bool tracker_finished(const tracker_t *tracker);
