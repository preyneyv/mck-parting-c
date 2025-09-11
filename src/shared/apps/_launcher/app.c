#include <stdio.h>

#include <shared/anim.h>
#include <shared/apps/apps.h>
#include <shared/engine.h>
#include <shared/utils/vec.h>

static const int16_t APP_SIZE = 36;
static const int16_t APP_MARGIN = 8;
static const int16_t APP_SCROLL_MARGIN =
    (DISP_WIDTH - (APP_SIZE * 2 + APP_MARGIN)) / 2;

static const uint8_t APP_COUNT = 6;
static app_t *apps[] = {
    &app_bongocat, &app_dummy,    &app_bongocat,
    &app_dummy,    &app_bongocat, &app_dummy,
};

static struct {
  int8_t active;
  bool ignore_right_release;
  int32_t active_offset;
  int32_t scroll_offset;
  uint32_t held_width;
} state = {
    .scroll_offset = APP_SCROLL_MARGIN,
};

static inline int32_t app_x(uint8_t app_index) {
  return (APP_SIZE + APP_MARGIN) * app_index;
}

static void change_active(int8_t delta) {
  state.active += delta;
  if (state.active < 0) {
    state.active = APP_COUNT - 1;
  } else if (state.active >= APP_COUNT) {
    state.active = 0;
  }

  // scroll to active
  int32_t offset = app_x(state.active);
  anim_to(&state.active_offset, offset, 150, ANIM_EASE_INOUT_QUAD, NULL, NULL);
  int32_t scroll_idx = state.active / 2;
  int32_t scroll_offset =
      APP_SCROLL_MARGIN - (APP_SIZE + APP_MARGIN) * 2 * scroll_idx;
  anim_to(&state.scroll_offset, scroll_offset, 150, ANIM_EASE_INOUT_QUAD, NULL,
          NULL);
}

static inline float ease_out_cubic(float t) {
  float inv = 1.0f - t;
  return 1.0f - inv * inv * inv;
}

static void enter() {
  state.ignore_right_release = false;
  state.held_width = 0;
}

static void frame() {
  bool ignore_left_release = false;

  if (BUTTON_PRESSED(BUTTON_RIGHT)) {
    float held = engine_button_held_ratio(BUTTON_RIGHT);
    ignore_left_release = state.ignore_right_release = held > 0.f;
    if (held >= 1.f) {
      engine_set_app(apps[state.active]);
    }
    if (held > 0) {
      anim_cancel(&state.held_width, false);
      state.held_width = APP_SIZE * ease_out_cubic(held);
    } // otherwise use the existing value instead of overwriting it.
  }

  if (BUTTON_KEYUP(BUTTON_LEFT) && !ignore_left_release) {
    change_active(-1);
  }

  if (BUTTON_KEYUP(BUTTON_RIGHT)) {
    anim_to(&state.held_width, 0, 150, ANIM_EASE_OUT_CUBIC, NULL, NULL);
    if (!state.ignore_right_release)
      change_active(1);
  }

  u8g2_t *u8g2 = &g_engine.display.u8g2;

  u8g2_SetDrawColor(u8g2, 1);
  for (int i = 0; i < APP_COUNT; i++) {
    vec2_t pos = vec2(state.scroll_offset + app_x(i), 11);
    u8g2_SetDrawColor(u8g2, 1);
    if (apps[i]->icon) {
      u8g2_DrawXBM(u8g2, pos.x, pos.y, APP_SIZE, APP_SIZE, apps[i]->icon);
    }
    if (i == state.active) {
      u8g2_SetDrawColor(u8g2, 2);
      u8g2_DrawBox(u8g2, pos.x + APP_SIZE - state.held_width, pos.y,
                   state.held_width, APP_SIZE);
    }
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawRFrame(u8g2, pos.x, pos.y, APP_SIZE, APP_SIZE, 1);
  }

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawRFrame(u8g2, state.scroll_offset + state.active_offset - 2, 9,
                  APP_SIZE + 4, APP_SIZE + 4, 3);

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  char *name = apps[state.active]->name;
  uint16_t width = u8g2_GetStrWidth(u8g2, name);
  u8g2_DrawStr(u8g2, (DISP_WIDTH - width) / 2, DISP_HEIGHT - 5,
               apps[state.active]->name);

  // status bar
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  char chg_str[8] = "CHG\0";
  if (!g_engine.peripheral.charging) {
    snprintf(chg_str, sizeof(chg_str), "%d", g_engine.peripheral.battery_level);
  }
  u8g2_DrawStr(u8g2, 128 - u8g2_GetStrWidth(u8g2, chg_str), 6, chg_str);

  u8g2_DrawStr(u8g2, 0, 6, "prism");
}

static void pause() {
  anim_cancel(&state.held_width, false);
  state.held_width = 0;
}

app_t app_launcher = {
    .name = "prism",
    .enter = enter,
    .frame = frame,
    .pause = pause,
};
