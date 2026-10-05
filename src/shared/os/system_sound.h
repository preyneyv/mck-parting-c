#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <shared/audio/synth.h>
#include <platform/time.h>

void system_sound_init(audio_synth_t *synth);
void system_sound_tick(platform_time_t now);
void system_sound_navigation(void);
void system_sound_hold(uint8_t step);

/* Play the next hold tick when a hold's progress (0..1) reaches a new step.
 * `step` tracks the last step played; reset it to 0 when the hold ends. */
static inline void system_sound_hold_progress(uint8_t *step, float ratio)
{
  uint8_t next = (uint8_t)(ratio * 5.f) + 1u;
  if (next > 5u)
    next = 5u;
  if (next != *step)
  {
    *step = next;
    system_sound_hold(next);
  }
}
void system_sound_menu(bool opening);
void system_sound_wake(uint8_t step);
