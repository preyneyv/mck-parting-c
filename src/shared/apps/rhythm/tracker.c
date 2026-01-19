#include "tracker.h"

#include <string.h>

#define TRACKER_DEFAULT_NOTE_MS 90

static void tracker_note_off(tracker_t *tracker, uint8_t voice)
{
    audio_synth_enqueue(tracker->synth,
                        &(audio_synth_message_t){
                            .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                            .data.note_off = {.voice = voice},
                        });
}

static void tracker_note_on(tracker_t *tracker, const tracker_note_t *note,
                            uint32_t now_ms)
{
    uint8_t voice = note->voice;
    if (voice >= AUDIO_SYNTH_VOICE_COUNT)
    {
        return;
    }

    if (tracker->state.voice_state[voice].active)
    {
        tracker_note_off(tracker, voice);
    }

    audio_synth_enqueue(tracker->synth,
                        &(audio_synth_message_t){
                            .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                            .data.note_on = {.voice = voice,
                                             .note_number = note->note,
                                             .velocity = note->velocity},
                        });

    uint16_t duration = note->duration_ms;
    if (duration == 0)
    {
        duration = TRACKER_DEFAULT_NOTE_MS;
    }

    tracker->state.voice_state[voice].active = true;
    tracker->state.voice_state[voice].off_ms = now_ms + duration;
}

void tracker_init(tracker_t *tracker, audio_synth_t *synth,
                  const tracker_chart_t *chart)
{
    memset(tracker, 0, sizeof(*tracker));
    tracker->synth = synth;
    if (chart != NULL)
    {
        tracker->chart = *chart;
    }
}

void tracker_set_chart(tracker_t *tracker, const tracker_chart_t *chart)
{
    if (chart == NULL)
    {
        tracker->chart.notes = NULL;
        tracker->chart.count = 0;
        return;
    }
    tracker->chart = *chart;
}

void tracker_set_voice_config(tracker_t *tracker, uint8_t voice,
                              const tracker_voice_config_t *cfg)
{
    if (tracker->synth == NULL || cfg == NULL)
    {
        return;
    }
    if (voice >= AUDIO_SYNTH_VOICE_COUNT)
    {
        return;
    }

    for (uint8_t op = 0; op < AUDIO_SYNTH_OPERATOR_COUNT; op++)
    {
        audio_synth_operator_set_config(&tracker->synth->voices[voice].ops[op],
                                        cfg->ops[op]);
    }
}

void tracker_start(tracker_t *tracker, uint32_t start_ms)
{
    if (tracker == NULL)
    {
        return;
    }
    tracker->state.start_ms = start_ms;
    tracker->state.next_index = 0;
    for (uint8_t voice = 0; voice < AUDIO_SYNTH_VOICE_COUNT; voice++)
    {
        tracker->state.voice_state[voice].active = false;
        tracker->state.voice_state[voice].off_ms = 0;
    }
}

void tracker_stop(tracker_t *tracker)
{
    if (tracker == NULL || tracker->synth == NULL)
    {
        return;
    }
    for (uint8_t voice = 0; voice < AUDIO_SYNTH_VOICE_COUNT; voice++)
    {
        if (tracker->state.voice_state[voice].active)
        {
            tracker_note_off(tracker, voice);
            tracker->state.voice_state[voice].active = false;
            tracker->state.voice_state[voice].off_ms = 0;
        }
    }
}

void tracker_tick(tracker_t *tracker, uint32_t now_ms)
{
    if (tracker == NULL || tracker->chart.notes == NULL)
    {
        return;
    }

    for (uint8_t voice = 0; voice < AUDIO_SYNTH_VOICE_COUNT; voice++)
    {
        if (tracker->state.voice_state[voice].active &&
            now_ms >= tracker->state.voice_state[voice].off_ms)
        {
            tracker_note_off(tracker, voice);
            tracker->state.voice_state[voice].active = false;
            tracker->state.voice_state[voice].off_ms = 0;
        }
    }

    uint32_t song_ms = now_ms - tracker->state.start_ms;
    while (tracker->state.next_index < tracker->chart.count)
    {
        const tracker_note_t *note = &tracker->chart.notes[tracker->state.next_index];
        if (note->time_ms > song_ms)
        {
            break;
        }
        tracker_note_on(tracker, note, now_ms);
        tracker->state.next_index++;
    }
}

bool tracker_finished(const tracker_t *tracker)
{
    if (tracker == NULL)
    {
        return true;
    }
    return tracker->state.next_index >= tracker->chart.count;
}
