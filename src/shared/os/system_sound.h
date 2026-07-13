#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <shared/audio/synth.h>
#include <platform/time.h>

void system_sound_init(audio_synth_t *synth);
void system_sound_tick(platform_time_t now);
void system_sound_navigation(void);
void system_sound_hold(uint8_t step);
void system_sound_menu(bool opening);
void system_sound_wake(uint8_t step);
