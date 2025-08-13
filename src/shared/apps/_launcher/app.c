#include <stdio.h>

#include <shared/anim.h>
#include <shared/apps/apps.h>
#include <shared/engine.h>

static const int32_t HOLD_MS_MIN = 300;
static const int32_t HOLD_MS_CONFIRM = 1000;

static const uint8_t APP_COUNT = 2;
static engine_app_t *apps[] = {
    &app_bongocat,
    &app_dummy,
};

static struct {
  uint8_t active;
} state;

static void frame() {
  bool ignore_right_release = false;
  if (BUTTON_PRESSED(BUTTON_RIGHT)) {
    int32_t ms =
        absolute_time_diff_us(g_engine.buttons.right.pressed_at, g_engine.now) /
        1000;
    float held = (ms - HOLD_MS_MIN) / (float)(HOLD_MS_CONFIRM - HOLD_MS_MIN);
    if (held > 0) {
      ignore_right_release = true;
    }
    if (held >= 1.f) {
      engine_set_app(apps[state.active]);
    }
  }
  if (BUTTON_KEYUP(BUTTON_LEFT)) {
    // key released
    if (state.active > 0) {
      state.active--;
    } else {
      state.active = APP_COUNT - 1;
    }
  }
  if (BUTTON_KEYUP(BUTTON_RIGHT) && !ignore_right_release) {
    // key released
    if (state.active < APP_COUNT - 1) {
      state.active++;
    } else {
      state.active = 0;
    }
  }
  u8g2_t *u8g2 = &g_engine.display.u8g2;
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
  for (int i = 0; i < APP_COUNT; i++) {
    if (i == state.active) {
      u8g2_DrawStr(u8g2, 0, 10 + i * 10, "> ");
    } else {
      u8g2_DrawStr(u8g2, 0, 10 + i * 10, "  ");
    }
    u8g2_DrawStr(u8g2, 7, 10 + i * 10, apps[i]->name);
  }

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);
  char chg_str[8] = "CHG\0";
  if (!g_engine.peripheral.charging) {
    snprintf(chg_str, sizeof(chg_str), "%d", g_engine.peripheral.battery_level);
  }
  u8g2_DrawStr(u8g2, 128 - u8g2_GetStrWidth(u8g2, chg_str), 4, chg_str);
}

engine_app_t app_launcher = {
    .name = "launcher",
    .enter = enter,
    .frame = frame,
};
