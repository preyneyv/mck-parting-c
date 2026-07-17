#include "onboarding.h"

#include <string.h>

#include <platform/display.h>
#include <prism/graphics/layout.h>
#include <prism/registry.h>
#include <prism/runtime.h>
#include <shared/anim.h>
#include <shared/engine.h>
#include <shared/os/launcher.h>
#include <shared/os/settings.h>
#include <shared/os/system_sound.h>

enum
{
  WELCOME_BADGE_SIZE = 30,
  WELCOME_BADGE_RADIUS = 2,
  WELCOME_FILL_SCALE = 1024,
  WELCOME_OUTLINE_SCALE = 256,
};

#define GUIDE_CARTRIDGE_ID "dev.preyneyv.prism.guide"

static struct
{
  int32_t fill;
  int32_t left_outline;
  int32_t right_outline;
  bool returning;
  bool completed;
  bool left_pressed;
  bool right_pressed;
  uint8_t hold_sound_step;
} welcome;

typedef struct
{
  int8_t x;
  int8_t y;
  uint8_t step_ms;
  uint8_t phase_offset;
} welcome_sparkle_t;

static const welcome_sparkle_t sparkles[] = {
    {10, 8, 95, 0},
    {117, 11, 110, 3},
    {7, 34, 85, 5},
    {121, 39, 105, 1},
    {14, 56, 120, 4},
    {113, 57, 90, 6},
    {64, 18, 130, 2},
};

static void centered_text(u8g2_t *u8g2, int16_t y, const char *text)
{
  uint16_t width = u8g2_GetStrWidth(u8g2, text);
  u8g2_DrawStr(u8g2, (DISP_WIDTH - width) / 2, y, text);
}

static void draw_sparkle(u8g2_t *u8g2, int16_t x, int16_t y,
                         uint32_t phase)
{
  phase %= 8u;
  if (phase == 0u || phase == 7u)
    return;

  u8g2_DrawPixel(u8g2, x, y);
  uint8_t radius = phase == 4u                  ? 2u
                   : phase >= 3u && phase <= 5u ? 1u
                                                : 0u;
  if (radius != 0u)
  {
    u8g2_DrawPixel(u8g2, x - radius, y);
    u8g2_DrawPixel(u8g2, x + radius, y);
    u8g2_DrawPixel(u8g2, x, y - radius);
    u8g2_DrawPixel(u8g2, x, y + radius);
  }
  if (phase == 4u)
  {
    u8g2_DrawPixel(u8g2, x - 1, y - 1);
    u8g2_DrawPixel(u8g2, x + 1, y - 1);
    u8g2_DrawPixel(u8g2, x - 1, y + 1);
    u8g2_DrawPixel(u8g2, x + 1, y + 1);
  }
}

static void draw_badge(u8g2_t *u8g2, elm_t *root, int16_t x,
                       const char *label, int32_t outline, float ratio,
                       bool from_right)
{
  const int16_t y = 19;
  int16_t outline_x = (int16_t)((2 * outline +
                                 WELCOME_OUTLINE_SCALE / 2) /
                                WELCOME_OUTLINE_SCALE);
  int16_t outline_y = (int16_t)((2 * outline +
                                 WELCOME_OUTLINE_SCALE / 2) /
                                WELCOME_OUTLINE_SCALE);
  u8g2_SetDrawColor(u8g2, 1);
  if (outline_x != 0 || outline_y != 0)
  {
    if (!from_right)
      outline_x = -outline_x;
    u8g2_DrawRFrame(u8g2, x + outline_x, y + outline_y,
                    WELCOME_BADGE_SIZE, WELCOME_BADGE_SIZE,
                    1);
    /* Keep only the exposed portion of the echo outline. */
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, x, y, WELCOME_BADGE_SIZE, WELCOME_BADGE_SIZE);
    u8g2_SetDrawColor(u8g2, 1);
  }
  u8g2_DrawRFrame(u8g2, x, y, WELCOME_BADGE_SIZE, WELCOME_BADGE_SIZE,
                  WELCOME_BADGE_RADIUS);
  u8g2_SetFont(u8g2, u8g2_font_7x14B_mr);
  uint16_t label_width = u8g2_GetStrWidth(u8g2, label);
  u8g2_DrawStr(u8g2, x + 1 + (WELCOME_BADGE_SIZE - label_width) / 2, y + 20,
               label);
  elm_rounded_hold_fill(root, vec2(x, y), WELCOME_BADGE_SIZE,
                        WELCOME_BADGE_SIZE, WELCOME_BADGE_RADIUS - 1, ratio,
                        from_right);
}

static void update_outline(bool pressed, bool *previous,
                           volatile int32_t *outline)
{
  if (pressed == *previous)
    return;
  *previous = pressed;
  anim_cancel(outline, false);
  anim_sys_to(outline, pressed ? WELCOME_OUTLINE_SCALE : 0,
              pressed ? 110u : 180u, ANIM_EASE_OUT_CUBIC, NULL, NULL);
}

static void welcome_enter(void)
{
  anim_cancel(&welcome.fill, false);
  anim_cancel(&welcome.left_outline, false);
  anim_cancel(&welcome.right_outline, false);
  memset(&welcome, 0, sizeof(welcome));
}

static void welcome_resume(void) { welcome_enter(); }

static void welcome_tick(void)
{
  bool left_pressed = engine_button_pressed(BUTTON_LEFT);
  bool right_pressed = engine_button_pressed(BUTTON_RIGHT);
  update_outline(left_pressed, &welcome.left_pressed,
                 &welcome.left_outline);
  update_outline(right_pressed, &welcome.right_pressed,
                 &welcome.right_outline);
  float left = engine_button_held_ratio(BUTTON_LEFT);
  float right = engine_button_held_ratio(BUTTON_RIGHT);
  float ratio = left_pressed && right_pressed
                    ? (left < right ? left : right)
                    : 0.f;

  if (ratio > 0.f)
  {
    anim_cancel(&welcome.fill, false);
    welcome.returning = false;
    welcome.fill = (int32_t)(ratio * WELCOME_FILL_SCALE);
    uint8_t step = (uint8_t)(ratio * 5.f) + 1u;
    if (step > 5u)
      step = 5u;
    if (step != welcome.hold_sound_step)
    {
      welcome.hold_sound_step = step;
      system_sound_hold(step);
    }
  }
  else
  {
    welcome.hold_sound_step = 0;
    if (welcome.fill > 0 && !welcome.returning)
    {
      welcome.returning = true;
      anim_sys_to(&welcome.fill, 0, 160, ANIM_EASE_OUT_CUBIC, NULL, NULL);
    }
    if (welcome.fill == 0)
      welcome.returning = false;
  }

  if (ratio >= 1.f && !welcome.completed)
  {
    welcome.completed = true;
    prism_settings_complete_first_interaction();
    /* The wake rule must be durable before guide is launched. */
    prism_settings_flush();
    engine_request_home();
  }
}

static void welcome_frame(void)
{
  u8g2_t *u8g2 = platform_display_get_u8g2();
  elm_t root = elm_root(u8g2, VEC2_Z);
  float ratio = welcome.fill / (float)WELCOME_FILL_SCALE;
  if (ratio > 1.f)
    ratio = 1.f;

  u8g2_SetDrawColor(u8g2, 1);
  uint32_t now_ms = (uint32_t)(engine_now() / 1000u);
  for (size_t i = 0; i < sizeof(sparkles) / sizeof(sparkles[0]); ++i)
    draw_sparkle(u8g2, sparkles[i].x, sparkles[i].y,
                 now_ms / sparkles[i].step_ms +
                     sparkles[i].phase_offset);

  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  centered_text(u8g2, 10, "say hi to prism!");
  draw_badge(u8g2, &root, 24, "L", welcome.left_outline, ratio, false);
  draw_badge(u8g2, &root, 74, "R", welcome.right_outline, ratio, true);

  /* Center the plus on the midpoint between the two badge centers. */
  u8g2_DrawLine(u8g2, 60, 34, 68, 34);
  u8g2_DrawLine(u8g2, 64, 30, 64, 38);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  centered_text(u8g2, 62, "hold both");

  for (uint8_t led = 0; led < 2; ++led)
  {
    bool pressed = led == 0 ? engine_button_pressed(BUTTON_LEFT)
                            : engine_button_pressed(BUTTON_RIGHT);
    uint8_t level = !pressed ? 0u : (uint8_t)(42.f + ratio * 213.f);
    engine_led_set(led, color_scale(prism_settings_led_color(led),
                                    level / 255.f));
  }
}

static app_t app_first_interaction = {
    .name = "welcome",
    .tick = welcome_tick,
    .frame = welcome_frame,
    .enter = welcome_enter,
    .resume = welcome_resume,
};

bool onboarding_first_interaction_active(void)
{
  return engine_is_app(&app_first_interaction);
}

void onboarding_go_home(void)
{
  const prism_cartridge_t *current = prism_cartridge_current();
  if (prism_settings_guide_pending() &&
      prism_cartridge_current_is_onboarding() && current != NULL &&
      strcmp(current->id, GUIDE_CARTRIDGE_ID) == 0)
  {
    prism_settings_dismiss_guide();
    prism_settings_flush();
  }

  if (!prism_settings_first_interaction_complete())
  {
    engine_set_app(&app_first_interaction);
    return;
  }

  if (prism_settings_guide_pending())
  {
    const prism_registry_entry_t *guide =
        prism_registry_find(GUIDE_CARTRIDGE_ID);
    if (guide != NULL)
    {
      if (prism_cartridge_launch_onboarding(guide->cartridge))
        return;
      /* A transient launch failure must not consume onboarding. */
    }
    else
    {
      /* Missing guide is a dismissal, so reinstalling it later remains an
       * ordinary manual cartridge install. */
      prism_settings_dismiss_guide();
      prism_settings_flush();
    }
  }

  engine_set_app(&app_launcher);
}
