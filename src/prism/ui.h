#pragma once

#include <stdint.h>

#include <prism/cartridge.h>

#define PRISM_UI_HOLD_TICK_COUNT 5u

typedef struct prism_ui_hold_feedback
{
  uint8_t step;
} prism_ui_hold_feedback_t;

static inline void prism_ui_navigate(prism_t *prism)
{
  prism_ui_sound(prism, PRISM_UI_SOUND_NAVIGATE, 0);
}

static inline void prism_ui_hold_feedback_reset(
    prism_ui_hold_feedback_t *feedback)
{
  feedback->step = 0;
}

static inline void prism_ui_hold_feedback_update(
    prism_t *prism, prism_ui_hold_feedback_t *feedback, float hold_ratio)
{
  if (hold_ratio <= 0.f)
  {
    prism_ui_hold_feedback_reset(feedback);
    return;
  }

  uint8_t step =
      (uint8_t)(hold_ratio * (float)PRISM_UI_HOLD_TICK_COUNT) + 1u;
  if (step > PRISM_UI_HOLD_TICK_COUNT)
    step = PRISM_UI_HOLD_TICK_COUNT;
  if (step <= feedback->step)
    return;

  feedback->step = step;
  prism_ui_sound(prism, PRISM_UI_SOUND_HOLD_TICK, step);
}
