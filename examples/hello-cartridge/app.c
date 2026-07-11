#include <stdio.h>

#include <prism/sdk.h>

#include "icon.h"

typedef struct
{
  uint32_t presses;
} hello_state_t;

static void tick(prism_t *prism)
{
  hello_state_t *state = prism->state;
  if (prism_button_keydown(prism, PRISM_BUTTON_LEFT) ||
      prism_button_keydown(prism, PRISM_BUTTON_RIGHT))
    ++state->presses;
}

static void frame(prism_t *prism)
{
  hello_state_t *state = prism->state;
  u8g2_t *u8g2 = prism_display(prism);

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  u8g2_DrawStr(u8g2, 31, 24, "HELLO PRISM");

  char count[24];
  snprintf(count, sizeof(count), "PRESSES: %lu",
           (unsigned long)state->presses);
  uint16_t width = u8g2_GetStrWidth(u8g2, count);
  u8g2_DrawStr(u8g2, (128 - width) / 2, 42, count);
}

PRISM_CARTRIDGE(cartridge_hello, 0x1000, "hello", "hello", hello_icon,
                PRISM_CARTRIDGE_FLAG_NONE, sizeof(hello_state_t),
                NULL, tick, frame, NULL, NULL, NULL);
