#include <stdio.h>

#include <shared/apps/apps.h>
#include <shared/engine.h>

static const int32_t HOLD_MS_MIN = 300;
static const int32_t HOLD_MS_CONFIRM = 1000;

static const uint8_t APP_COUNT = 2;
static engine_app_t *apps[] = {
    &app_bongocat,
    &app_dummy,
};

static uint8_t active = 0;

static void frame() {
  bool ignore_right_release = false;
  if (engine.buttons.right.pressed) {
    // printf("hi \n");

    int32_t ms =
        absolute_time_diff_us(engine.buttons.right.pressed_at, engine.now) /
        1000;
    float held = (ms - HOLD_MS_MIN) / (float)(HOLD_MS_CONFIRM - HOLD_MS_MIN);
    if (held > 0) {
      ignore_right_release = true;
    }

    printf("holding %f\n", held);
    if (held >= 1.f) {
      engine_set_app(apps[active]);
    }
  }
  if (!engine.buttons.left.pressed && engine.buttons.left.evt) {
    // key released
    if (active > 0) {
      active--;
    } else {
      active = APP_COUNT - 1;
    }
  }
  // printf("ignore_right_release: %d\n", ignore_right_release);
  if (!engine.buttons.right.pressed && engine.buttons.right.evt &&
      !ignore_right_release) {
    // key released
    if (active < APP_COUNT - 1) {
      active++;
    } else {
      active = 0;
    }
  }
  u8g2_t *u8g2 = &engine.display.u8g2;
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
  for (int i = 0; i < APP_COUNT; i++) {
    if (i == active) {
      u8g2_DrawStr(u8g2, 0, 10 + i * 10, "> ");
    } else {
      u8g2_DrawStr(u8g2, 0, 10 + i * 10, "  ");
    }
    u8g2_DrawStr(u8g2, 7, 10 + i * 10, apps[i]->name);
  }

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);
  char chg_str[8] = "CHG\0";
  if (!engine.peripheral.charging) {
    snprintf(chg_str, sizeof(chg_str), "%d", engine.peripheral.battery_level);
  }
  u8g2_DrawStr(u8g2, 128 - u8g2_GetStrWidth(u8g2, chg_str), 4, chg_str);
}

engine_app_t app_launcher = {
    .name = "launcher",
    .frame = frame,
};
