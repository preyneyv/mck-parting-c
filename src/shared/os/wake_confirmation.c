#include "wake_confirmation.h"

#include <string.h>

#include <platform/display.h>
#include <platform/time.h>
#include <prism/graphics/layout.h>
#include <shared/anim.h>
#include <shared/config.h>
#include <shared/engine.h>
#include <shared/os/settings.h>

#include "engine_internal.h"

static struct
{
  bool active;
  bool completing;
  platform_input_mask_t button;
  uint8_t count;
  platform_time_t idle_deadline;
  platform_time_t led_flash_started;
  volatile int32_t circle_radius[3];
  volatile int32_t container_y;
} wake;

static struct
{
  bool active;
  platform_time_t started;
  color_t from[LED_COUNT];
} release;

static bool led_selected(platform_input_mask_t button, uint8_t led)
{
  return (button == PLATFORM_INPUT_LEFT && led == LED_L) ||
         (button == PLATFORM_INPUT_RIGHT && led == LED_R);
}

static color_t led_current(uint8_t led)
{
  if (!led_selected(wake.button, led))
    return (color_t){.hex = 0};

  color_t pattern = prism_settings_led_color(led);
  int64_t elapsed = platform_time_diff_us(wake.led_flash_started,
                                          platform_now_us());
  float t = elapsed <= 0 ? 0.f : elapsed >= 240000 ? 1.f
                                                   : elapsed / 240000.f;
  t = 1.f - (1.f - t) * (1.f - t) * (1.f - t);
  return color_lerp(rgba(255, 255, 255, 255), pattern, t);
}

static void exit_done(void *ctx)
{
  (void)ctx;
  release.active = true;
  release.started = platform_now_us();
  for (uint8_t i = 0; i < LED_COUNT; ++i)
    release.from[i] = led_current(i);
  engine_finish_wake();
}

static void circle_settled(void *ctx)
{
  uint8_t index = (uint8_t)(uintptr_t)ctx;
  if (wake.active && wake.completing && index == 2)
    anim_sys_to(&wake.container_y, -DISP_HEIGHT, 300, ANIM_EASE_OUT_CUBIC,
                exit_done, NULL);
}

static void pop_circle(uint8_t index)
{
  anim_cancel(&wake.circle_radius[index], false);
  wake.circle_radius[index] = 5;
  anim_sys_to(&wake.circle_radius[index], 10, 150, ANIM_EASE_OUT_CUBIC,
              circle_settled, (void *)(uintptr_t)index);
}

bool wake_confirmation_active(void) { return wake.active; }

bool wake_confirmation_expired(void)
{
  return wake.active && platform_time_reached(wake.idle_deadline);
}

void wake_confirmation_start(platform_input_mask_t button)
{
  wake.active = true;
  wake.completing = false;
  wake.button = button;
  wake.count = 1;
  wake.container_y = 0;
  wake.idle_deadline = platform_time_add_ms(platform_now_us(), 5000);
  wake.led_flash_started = platform_now_us();
  for (uint8_t i = 0; i < 3; ++i)
  {
    anim_cancel(&wake.circle_radius[i], false);
    wake.circle_radius[i] = 5;
  }
  anim_cancel(&wake.container_y, false);
  pop_circle(0);
}

void wake_confirmation_cancel(void)
{
  for (uint8_t i = 0; i < 3; ++i)
    anim_cancel(&wake.circle_radius[i], false);
  anim_cancel(&wake.container_y, false);
  wake.active = false;
  wake.completing = false;
}

static platform_input_mask_t pressed_button(void)
{
  if (BUTTON_KEYDOWN(BUTTON_LEFT))
    return PLATFORM_INPUT_LEFT;
  if (BUTTON_KEYDOWN(BUTTON_RIGHT))
    return PLATFORM_INPUT_RIGHT;
  if (BUTTON_KEYDOWN(BUTTON_MENU))
    return PLATFORM_INPUT_MENU;
  return 0;
}

void wake_confirmation_tick(void)
{
  if (wake.completing)
    return;
  platform_input_mask_t pressed = pressed_button();
  if (pressed == 0)
    return;

  if (pressed == wake.button)
    ++wake.count;
  else
  {
    wake.button = pressed;
    wake.count = 1;
    for (uint8_t i = 0; i < 3; ++i)
    {
      anim_cancel(&wake.circle_radius[i], false);
      wake.circle_radius[i] = 5;
    }
  }
  wake.idle_deadline = platform_time_add_ms(platform_now_us(), 5000);
  wake.led_flash_started = platform_now_us();

  if (wake.count >= 3)
  {
    wake.count = 3;
    wake.completing = true;
    pop_circle(2);
  }
  else
  {
    pop_circle(wake.count - 1);
  }
}

void wake_confirmation_frame(const uint8_t *app_framebuffer)
{
  u8g2_t *u8g2 = platform_display_get_u8g2();
  memcpy(u8g2_GetBufferPtr(u8g2), app_framebuffer,
         (DISP_WIDTH * DISP_HEIGHT) / 8);

  int16_t overlay_bottom = DISP_HEIGHT + wake.container_y;
  if (overlay_bottom > 0)
  {
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, 0, 0, DISP_WIDTH,
                 overlay_bottom > DISP_HEIGHT ? DISP_HEIGHT : overlay_bottom);
  }
  u8g2_SetDrawColor(u8g2, 1);
  const char *button = wake.button == PLATFORM_INPUT_LEFT ? "L" :
                       wake.button == PLATFORM_INPUT_RIGHT ? "R" : "M";
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  for (uint8_t i = 0; i < 3; ++i)
  {
    uint8_t x = 40 + i * 24;
    int16_t y = 32 + wake.container_y;
    uint8_t radius = (uint8_t)wake.circle_radius[i];
    if (y + radius < 0)
      continue;
    u8g2_DrawCircle(u8g2, x, y, radius, U8G2_DRAW_ALL);
    if (i < wake.count)
      u8g2_DrawStr(u8g2, x - u8g2_GetStrWidth(u8g2, button) / 2, y + 4,
                   button);
  }
  u8g2_DrawHLine(u8g2, 0, DISP_HEIGHT + wake.container_y, DISP_WIDTH);

  for (uint8_t i = 0; i < LED_COUNT; ++i)
    engine_led_set(i, led_current(i));
}

color_t wake_confirmation_blend_led(uint8_t led, color_t app_color)
{
  if (!release.active || led >= LED_COUNT)
    return app_color;
  int64_t elapsed = platform_time_diff_us(release.started, platform_now_us());
  float t = elapsed <= 0 ? 0.f : elapsed >= 220000 ? 1.f
                                                   : elapsed / 220000.f;
  t = t * t * (3.f - 2.f * t);
  return color_lerp(release.from[led], app_color, t);
}

void wake_confirmation_release_tick(void)
{
  if (release.active &&
      platform_time_diff_us(release.started, platform_now_us()) >= 220000)
    release.active = false;
}
