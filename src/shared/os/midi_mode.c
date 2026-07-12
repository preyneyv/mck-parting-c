#include <stdbool.h>

#include <platform/display.h>
#include <shared/engine.h>
#include <prism/graphics/layout.h>

#include "midi_mode.h"

static void tick(void)
{
  /* MIDI synth is an OS mode, not an idle application: it must remain awake
   * even when the controller leaves a note sustaining without more traffic. */
  engine_mark_input();
}

static void frame(void)
{
  u8g2_t *u8g2 = platform_display_get_u8g2();
  elm_t root = elm_root(u8g2, VEC2_Z);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_7x14B_mr);
  const char *title = "MIDI SYNTH";
  elm_str(&root, vec2((128 - u8g2_GetStrWidth(u8g2, title)) / 2, 31), title);

  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  const char *status = "USB INSTRUMENT";
  elm_str(&root, vec2((128 - u8g2_GetStrWidth(u8g2, status)) / 2, 44), status);

  elm_hline(&root, vec2(33, 52), 62);
  for (uint8_t x = 38; x < 94; x += 9)
    elm_vline(&root, vec2(x, 49), 7);
}

static app_t app_midi_mode = {
    .name = "midi synth",
    .tick = tick,
    .frame = frame,
};

bool prism_midi_mode_active(void) { return engine_is_app(&app_midi_mode); }

void prism_midi_mode_enter(void)
{
  if (!prism_midi_mode_active())
    engine_set_app(&app_midi_mode);
  engine_mark_input();
}

void prism_midi_mode_usb_disconnected(void)
{
  if (prism_midi_mode_active())
    engine_set_app(NULL);
}
