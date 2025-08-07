#include <shared/apps/apps.h>
#include <shared/engine.h>

static void draw() {
  u8g2_t *u8g2 = &engine.display.u8g2;
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawBox(u8g2, 0, 0, u8g2->width, u8g2->height);
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
  u8g2_DrawStr(u8g2, 0, 10, "launcher");
}

engine_app_t app_launcher = {
    .name = "launcher",
    .enter = NULL,
    .update = NULL,
    .draw = NULL,
    .pause = NULL,
    .resume = NULL,
    .exit = NULL,
};
