#include <platform/sleep.h>

#include <platform/display.h>
#include <platform/input.h>
#include <platform/platform.h>
#include <platform/time.h>
#include <shared/config.h>

#include <u8g2.h>

static const platform_input_mask_t BUTTON_MASK =
    PLATFORM_INPUT_LEFT | PLATFORM_INPUT_RIGHT | PLATFORM_INPUT_MENU;

static platform_input_mask_t first_button(platform_input_mask_t mask)
{
  if (mask & PLATFORM_INPUT_LEFT)
    return PLATFORM_INPUT_LEFT;
  if (mask & PLATFORM_INPUT_RIGHT)
    return PLATFORM_INPUT_RIGHT;
  if (mask & PLATFORM_INPUT_MENU)
    return PLATFORM_INPUT_MENU;
  return 0;
}

static void draw_sleeping(void)
{
  static const char label[] = "sleeping";
  u8g2_t *u8g2 = platform_display_get_u8g2();
  platform_display_set_enabled(true);
  /* Auto-sleep begins after the idle fade has reached zero contrast. Restore
   * host visibility before presenting the simulator-only sleeping screen;
   * the engine reapplies the configured contrast on wake. */
  platform_display_set_contrast(255);
  u8g2_ClearBuffer(u8g2);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  u8g2_uint_t width = u8g2_GetStrWidth(u8g2, label);
  u8g2_DrawStr(u8g2, (DISP_WIDTH - width) / 2u, DISP_HEIGHT / 2u + 4u,
               label);
  u8g2_SendBuffer(u8g2);
}

platform_wake_result_t platform_sleep_enter(uint32_t quick_wake_ms)
{
  draw_sleeping();

  /* The key that selected sleep must be released before it can also be the
   * first wake-confirmation press. */
  while ((platform_input_read_mask() & BUTTON_MASK) != 0)
  {
    platform_task();
    platform_sleep_ms(5);
  }

  platform_time_t quick_wake_until =
      platform_now_us() + (platform_time_t)quick_wake_ms * 1000u;
  bool quick_wake = quick_wake_ms != 0;
  while (true)
  {
    platform_task();
    platform_input_mask_t mask = platform_input_read_mask() & BUTTON_MASK;
    if (mask != 0)
      return (platform_wake_result_t){
          .confirmation_required = !quick_wake,
          .wake_button = first_button(mask),
      };

    if (quick_wake && platform_time_reached(quick_wake_until))
      quick_wake = false;
    platform_sleep_ms(5);
  }
}
