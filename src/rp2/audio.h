#pragma once

#include <shared/audio/synth.h>

void audio_playback_init();
void audio_playback_run_forever(audio_synth_t *synth);
