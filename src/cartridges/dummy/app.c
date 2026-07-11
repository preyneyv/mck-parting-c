#include <stdio.h>

#include <prism/sdk.h>

static void frame(prism_t *prism) {
  static uint8_t i = 0;
  if (prism_button_keydown(prism, PRISM_BUTTON_RIGHT) ||
      prism_button_keydown(prism, PRISM_BUTTON_LEFT)) {
    prism_sleep(prism);
  }
  u8g2_t *u8g2 = prism_display(prism);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
  u8g2_SetDrawColor(u8g2, 1);

  char str[32];
  snprintf(str, sizeof(str), "i: %d", i++);
  u8g2_DrawStr(u8g2, 0, 20, str);
  i += 1;
}

PRISM_CARTRIDGE(cartridge_dummy, 0x101, "dummy", "dummy", NULL,
                PRISM_CARTRIDGE_FLAG_NONE,
                0, NULL, NULL, frame, NULL, NULL, NULL);
