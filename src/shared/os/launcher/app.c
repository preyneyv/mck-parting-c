#include <shared/anim.h>
#include <shared/os/launcher.h>
#include <shared/os/power_indicator.h>
#include <shared/config.h>
#include <shared/engine.h>
#include <platform/display.h>
#include <platform/peripheral.h>
#include <prism/graphics/vector.h>
#include <prism/registry.h>
#include <prism/runtime.h>

static const int16_t APP_SIZE = 36;
static const int16_t APP_MARGIN = 8;
static const int16_t APP_SCROLL_MARGIN =
    (DISP_WIDTH - (APP_SIZE * 2 + APP_MARGIN)) / 2;

static inline uint8_t app_count(void)
{
  return (uint8_t)prism_registry_visible_count();
}

static struct
{
  int8_t active;
  bool ignore_release;
  int32_t active_offset;
  int32_t scroll_offset;
  int32_t held_width;
  button_id_t held_button;
} state = {
    .scroll_offset = APP_SCROLL_MARGIN,
};

static inline int32_t app_x(uint8_t app_index)
{
  return (APP_SIZE + APP_MARGIN) * app_index;
}

static void change_active(int8_t delta)
{
  uint8_t count = app_count();
  if (count == 0)
    return;
  state.active += delta;
  if (state.active < 0)
  {
    state.active = count - 1;
  }
  else if (state.active >= count)
  {
    state.active = 0;
  }

  int32_t offset = app_x(state.active);
  anim_to(&state.active_offset, offset, 150, ANIM_EASE_INOUT_QUAD, NULL, NULL);
  int32_t scroll_idx = state.active / 2;
  int32_t scroll_offset =
      APP_SCROLL_MARGIN - (APP_SIZE + APP_MARGIN) * 2 * scroll_idx;
  anim_to(&state.scroll_offset, scroll_offset, 150, ANIM_EASE_INOUT_QUAD, NULL,
          NULL);
}

static inline float ease_out_cubic(float t)
{
  float inv = 1.0f - t;
  return 1.0f - inv * inv * inv;
}

static void enter()
{
  state.ignore_release = false;
  state.held_width = 0;
}

static void frame()
{
  uint8_t count = app_count();
  if (count == 0)
    return;
  if (state.active < 0 || state.active >= count)
  {
    state.active = 0;
    state.active_offset = 0;
    state.scroll_offset = APP_SCROLL_MARGIN;
  }
  button_id_t button_id = engine_button_get_pressed_first();
  if (button_id != BUTTON_NONE)
  {
    float held = engine_button_held_ratio(button_id);
    state.ignore_release = held > 0.f;
    if (held >= 1.f)
    {
      prism_cartridge_launch(
          prism_registry_visible_get(state.active)->cartridge);
    }
    if (held > 0)
    {
      anim_cancel(&state.held_width, false);
      state.held_width = APP_SIZE * ease_out_cubic(held);
      state.held_button = button_id;
    }
  }

  if (BUTTON_KEYUP(BUTTON_LEFT))
  {
    anim_to(&state.held_width, 0, 150, ANIM_EASE_OUT_CUBIC, NULL, NULL);
    if (!state.ignore_release)
      change_active(-1);
  }

  if (BUTTON_KEYUP(BUTTON_RIGHT))
  {
    anim_to(&state.held_width, 0, 150, ANIM_EASE_OUT_CUBIC, NULL, NULL);
    if (!state.ignore_release)
      change_active(1);
  }

  u8g2_t *u8g2 = platform_display_get_u8g2();

  u8g2_SetDrawColor(u8g2, 1);
  for (int i = 0; i < count; i++)
  {
    const prism_cartridge_t *cartridge =
        prism_registry_visible_get(i)->cartridge;
    vec2_t pos = vec2(state.scroll_offset + app_x(i), 11);
    u8g2_SetDrawColor(u8g2, 1);
    if (cartridge->icon)
    {
      u8g2_DrawXBM(u8g2, pos.x, pos.y, APP_SIZE, APP_SIZE, cartridge->icon);
    }
    if (i == state.active)
    {
      u8g2_SetDrawColor(u8g2, 2);
      uint8_t box_x = 0;
      if (state.held_button == BUTTON_RIGHT)
      {
        box_x = pos.x + APP_SIZE - state.held_width;
      }
      else if (state.held_button == BUTTON_LEFT)
      {
        box_x = pos.x;
      }
      u8g2_DrawBox(u8g2, box_x, pos.y, state.held_width, APP_SIZE);
    }
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawRFrame(u8g2, pos.x, pos.y, APP_SIZE, APP_SIZE, 1);
  }

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawRFrame(u8g2, state.scroll_offset + state.active_offset - 2, 9,
                  APP_SIZE + 4, APP_SIZE + 4, 3);

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  const prism_cartridge_t *active =
      prism_registry_visible_get(state.active)->cartridge;
  const char *name = active->name;
  uint16_t width = u8g2_GetStrWidth(u8g2, name);
  u8g2_DrawStr(u8g2, (DISP_WIDTH - width) / 2, DISP_HEIGHT - 5,
               active->name);

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);

  platform_power_state_t power_state = platform_peripheral_get_power_state();
  power_indicator_draw(u8g2, DISP_WIDTH, 0, power_state);

  u8g2_DrawStr(u8g2, 0, 6, "prism");
}

static void pause()
{
  anim_cancel(&state.held_width, false);
  state.held_width = 0;
}

app_t app_launcher = {
    .name = "prism",
    .enter = enter,
    .frame = frame,
    .pause = pause,
};
