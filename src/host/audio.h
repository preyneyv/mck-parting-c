#pragma once

#include <stdbool.h>
#include <shared/audio/synth.h>

void audio_playback_init(void);
void audio_playback_run_forever(audio_synth_t *synth);
void audio_playback_set_enabled(bool enabled);
