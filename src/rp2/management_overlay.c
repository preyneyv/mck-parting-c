#include "management_overlay.h"

#include <stdio.h>
#include <string.h>

#include <platform/display.h>
#include <platform/time.h>
#include <prism/runtime.h>
#include <shared/config.h>
#include <shared/engine.h>
#include <u8g2.h>

typedef enum
{
  OVERLAY_HIDDEN,
  OVERLAY_INSTALL,
  OVERLAY_DELETE,
  OVERLAY_COMPACT,
} overlay_state_t;

static struct
{
  overlay_state_t state;
  uint32_t completed;
  uint32_t total;
  bool finished;
  bool failed;
  platform_time_t dismiss_at;
  char name[PRISM_CARTRIDGE_NAME_MAX + 1];
  uint8_t icon[PRISM_CARTRIDGE_ICON_BYTES];
} overlay;

static void overlay_tick(void)
{
  if (overlay.finished && platform_time_reached(overlay.dismiss_at))
  {
    overlay.state = OVERLAY_HIDDEN;
    engine_set_app(NULL);
  }
}

static void draw_centered(u8g2_t *u8g2, uint8_t x, uint8_t width,
                          uint8_t baseline, const char *text)
{
  uint8_t text_width = u8g2_GetStrWidth(u8g2, text);
  u8g2_DrawStr(u8g2, x + (width - text_width) / 2, baseline, text);
}

static app_t overlay_app = {
    .name = "management",
    .tick = overlay_tick,
};

static void draw_cartridge_operation(u8g2_t *u8g2, const char *title,
                                     uint32_t percent, bool show_progress)
{
  const uint8_t icon_x =
      (DISP_WIDTH - PRISM_CARTRIDGE_ICON_WIDTH) / 2;
  const uint8_t icon_y = 13;

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  draw_centered(u8g2, 0, DISP_WIDTH, 7,
                overlay.failed ? "FAILED" : title);

  u8g2_DrawXBM(u8g2, icon_x, icon_y, PRISM_CARTRIDGE_ICON_WIDTH,
               PRISM_CARTRIDGE_ICON_HEIGHT, overlay.icon);
  if (show_progress)
  {
    const uint8_t fill_width = PRISM_CARTRIDGE_ICON_WIDTH - 2;
    const uint8_t fill_height = PRISM_CARTRIDGE_ICON_HEIGHT - 2;
    uint8_t fill = (uint8_t)((percent * fill_height) / 100u);
    if (fill != 0)
    {
      u8g2_SetDrawColor(u8g2, 2);
      u8g2_DrawBox(u8g2, icon_x + 1,
                   icon_y + PRISM_CARTRIDGE_ICON_HEIGHT - 1 - fill,
                   fill_width, fill);
    }
  }

  /* Match the launcher: XOR the icon first, then redraw its frame so the
   * progress fill never inverts the border. */
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawRFrame(u8g2, icon_x, icon_y, PRISM_CARTRIDGE_ICON_WIDTH,
                  PRISM_CARTRIDGE_ICON_HEIGHT, 1);

  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  uint8_t name_baseline =
      (uint8_t)(DISP_HEIGHT - 1 + u8g2_GetDescent(u8g2));
  draw_centered(u8g2, 0, DISP_WIDTH, name_baseline, overlay.name);
}

void management_overlay_start(management_overlay_operation_t operation,
                              uint32_t total, const char *name,
                              const uint8_t *icon)
{
  overlay.state = operation == MANAGEMENT_OVERLAY_DELETE
                      ? OVERLAY_DELETE
                  : operation == MANAGEMENT_OVERLAY_COMPACT
                      ? OVERLAY_COMPACT
                      : OVERLAY_INSTALL;
  overlay.completed = 0;
  overlay.total = total == 0 ? 1 : total;
  overlay.finished = false;
  overlay.failed = false;
  memset(overlay.name, 0, sizeof(overlay.name));
  memset(overlay.icon, 0, sizeof(overlay.icon));
  if (name != NULL)
  {
    size_t length = strnlen(name, sizeof(overlay.name) - 1);
    memcpy(overlay.name, name, length);
  }
  if (icon != NULL)
    memcpy(overlay.icon, icon, sizeof(overlay.icon));
  engine_set_app(&overlay_app);
}

void management_overlay_set_completed(uint32_t completed)
{
  overlay.completed = completed;
}

void management_overlay_set_progress(uint32_t completed, uint32_t total)
{
  overlay.completed = completed;
  overlay.total = total == 0 ? 1 : total;
}

void management_overlay_finish(prism_management_status_t status)
{
  if (overlay.state == OVERLAY_HIDDEN)
    return;
  overlay_state_t operation = overlay.state;
  overlay.failed = status != PRISM_MGMT_OK;
  if (!overlay.failed)
    overlay.completed = overlay.total;
  overlay.finished = true;
  overlay.dismiss_at = !overlay.failed && operation != OVERLAY_DELETE
                           ? platform_now_us()
                           : platform_time_add_ms(platform_now_us(), 750);
}

void management_overlay_cancel_install(void)
{
  if (overlay.state != OVERLAY_INSTALL)
    return;
  memset(&overlay, 0, sizeof(overlay));
  engine_set_app(NULL);
}

bool management_overlay_active(void)
{
  return overlay.state != OVERLAY_HIDDEN;
}

void management_overlay_render(void)
{
  u8g2_t *u8g2 = platform_display_get_u8g2();
  u8g2_ClearBuffer(u8g2);
  const char *title = overlay.state == OVERLAY_DELETE
                          ? "UNINSTALLING"
                      : overlay.state == OVERLAY_COMPACT
                          ? "COMPACTING"
                          : "INSTALLING";
  const char *state = overlay.failed ? "FAILED" :
                      overlay.finished ? "DONE" : NULL;
  uint32_t percent = overlay.total == 0 ? 0 :
      (overlay.completed * 100u) / overlay.total;
  if (percent > 100u)
    percent = 100u;

  bool has_cartridge = (overlay.state == OVERLAY_INSTALL ||
                        overlay.state == OVERLAY_DELETE) &&
                       overlay.name[0] != '\0';
  if (has_cartridge)
  {
    bool installing = overlay.state == OVERLAY_INSTALL;
    draw_cartridge_operation(u8g2,
                             installing ? "installing" : "uninstalling",
                             percent, installing);
    return;
  }

  const uint8_t content_x = 8;
  const uint8_t content_width = 112;
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  draw_centered(u8g2, 0, DISP_WIDTH, 17, title);
  uint8_t bar_y = 27;
  u8g2_DrawRFrame(u8g2, content_x, bar_y, content_width, 13, 2);
  uint8_t inner_width = content_width - 4;
  uint8_t fill = (uint8_t)((percent * inner_width) / 100u);
  if (fill != 0)
    u8g2_DrawBox(u8g2, content_x + 2, bar_y + 2, fill, 9);

  char label[12];
  if (state != NULL)
    snprintf(label, sizeof(label), "%s", state);
  else
    snprintf(label, sizeof(label), "%lu%%", (unsigned long)percent);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  draw_centered(u8g2, 0, DISP_WIDTH, 57, label);
}
