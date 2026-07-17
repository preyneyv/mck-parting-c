#include <stdio.h>

#include <prism/graphics/layout.h>
#include <prism/ui.h>

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,   \
              #condition);                                                   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

static prism_ui_sound_t last_sound;
static uint8_t last_value;
static uint8_t sound_count;

static void ui_sound(prism_ui_sound_t sound, uint8_t value)
{
  last_sound = sound;
  last_value = value;
  sound_count++;
}

int main(void)
{
  u8g2_t display = {0};
  elm_t translated = elm_root(&display, vec2(128, 0));
  elm_t centered = elm_child_aligned(&translated, vec2(64, 48), 64, 14,
                                     ELM_ALIGN_TOP_CENTER);
  CHECK(centered.pos.x == 160);
  CHECK(centered.pos.y == 48);

  const prism_api_v1_t api = {.ui_sound = ui_sound};
  prism_t prism = {.api = &api};
  prism_ui_hold_feedback_t feedback = {0};

  prism_ui_hold_feedback_update(&prism, &feedback, 0.f);
  CHECK(sound_count == 0);

  prism_ui_hold_feedback_update(&prism, &feedback, .01f);
  CHECK(sound_count == 1);
  CHECK(last_sound == PRISM_UI_SOUND_HOLD_TICK);
  CHECK(last_value == 1);

  prism_ui_hold_feedback_update(&prism, &feedback, .19f);
  CHECK(sound_count == 1);
  prism_ui_hold_feedback_update(&prism, &feedback, .20f);
  CHECK(sound_count == 2);
  CHECK(last_value == 2);

  prism_ui_hold_feedback_update(&prism, &feedback, 1.f);
  CHECK(sound_count == 3);
  CHECK(last_value == PRISM_UI_HOLD_TICK_COUNT);
  prism_ui_hold_feedback_update(&prism, &feedback, .5f);
  CHECK(sound_count == 3);

  prism_ui_hold_feedback_update(&prism, &feedback, 0.f);
  prism_ui_hold_feedback_update(&prism, &feedback, .01f);
  CHECK(sound_count == 4);
  CHECK(last_value == 1);

  prism_ui_navigate(&prism);
  CHECK(sound_count == 5);
  CHECK(last_sound == PRISM_UI_SOUND_NAVIGATE);
  CHECK(last_value == 0);

  puts("UI feedback tests passed");
  return 0;
}
