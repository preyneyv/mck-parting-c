#include <stdio.h>

#include <shared/apps/apps.h>

static void frame() {
  static uint8_t i = 0;
  if (BUTTON_KEYDOWN(BUTTON_RIGHT)) {
    // enter sleep
    engine_enter_sleep();
  }
  u8g2_t *u8g2 = &g_engine.display.u8g2;
  u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
  u8g2_SetDrawColor(u8g2, 1);

  char str[32];
  snprintf(str, sizeof(str), "i: %d", i++);
  u8g2_DrawStr(u8g2, 0, 20, str);
  i += 1;
}

app_t app_dummy = {
    .name = "dummy",
    .frame = frame,
};
