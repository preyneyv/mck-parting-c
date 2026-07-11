#include <stdio.h>
#include <unistd.h>

#include <shared/config.h>
#include <platform/display.h>
#include <platform/input.h>
#include <platform/peripheral.h>
#include <platform/platform.h>
#include <platform/sleep.h>
#include <platform/system.h>
#include <platform/time.h>
#include <shared/utils/elm.h>
#include <shared/utils/color.h>
#include <shared/utils/misc.h>
#include <shared/utils/timing.h>
#include <shared/utils/vec.h>

#include "anim.h"
#include "os/launcher.h"
#include "os/settings.h"
#include <prism/runtime.h>
#include "engine.h"
// #define DEBUG_FPS

// global engine instance
engine_t g_engine;
static uint8_t last_app_framebuffer[(DISP_WIDTH * DISP_HEIGHT) / 8];

static struct
{
  bool active;
  platform_time_t started;
  color_t from[LED_COUNT];
} wake_release_transition;

// log-inspired volume curve for more perceptually linear volume steps.
static const q1x15 ENGINE_VOLUME_CURVE[9] = {
    Q1X15_ZERO, // mute
    2195,       // ~0.067
    5406,       // ~0.165
    9142,       // ~0.279
    13311,      // ~0.406
    17797,      // ~0.543
    22534,      // ~0.688
    27557,      // ~0.841
    Q1X15_ONE,  // full scale
};

void engine_init()
{
  // initialize all subsystems
  audio_synth_init(&g_engine.synth, AUDIO_SAMPLE_RATE, 1000);
  for (uint8_t i = 0; i < LED_COUNT; i++)
  {
    g_engine.led_colors[i] = (color_t){.hex = 0x000000};
  }

  g_engine.buttons.left.id = BUTTON_LEFT;
  g_engine.buttons.right.id = BUTTON_RIGHT;
  g_engine.buttons.menu.id = BUTTON_MENU;

  prism_settings_init();
  engine_set_app(NULL);
  engine_set_volume((int8_t)prism_settings_get()->volume);
  engine_set_brightness((int8_t)prism_settings_get()->brightness);
  g_engine.last_input_at = platform_now_us();
}

void engine_set_volume(int8_t level)
{
  int clamped = level;
  if (clamped < 0)
    clamped = 0;
  if (clamped > 8)
    clamped = 8;
  g_engine.volume = (uint8_t)clamped;
  g_engine.synth.master_level = ENGINE_VOLUME_CURVE[g_engine.volume];
}

inline void engine_change_volume(int8_t direction)
{
  engine_set_volume(g_engine.volume + direction);
  prism_settings_volume_changed(g_engine.volume);
}

uint8_t engine_brightness_scale(void)
{
  /* Perceptual output curve.  Low settings get finer control while the larger
   * top-end jumps keep 6/8, 7/8, and 8/8 visually distinct.  The minimum is
   * deliberately the old linear 1/8 value. */
  static const uint8_t brightness_curve[9] = {
      60, 68, 80, 96, 116, 142, 174, 212, 255,
  };
  return brightness_curve[g_engine.brightness];
}

uint8_t engine_output_brightness_scale(void)
{
  uint8_t configured = engine_brightness_scale();
  int64_t idle_us = platform_time_diff_us(g_engine.last_input_at,
                                          platform_now_us());
  const int64_t sleep_us = (int64_t)AUTO_SLEEP_TIMEOUT_MS * 1000;
  const int64_t fade_us = (int64_t)AUTO_SLEEP_FADE_MS * 1000;
  if (idle_us <= sleep_us - fade_us)
    return configured;
  if (idle_us >= sleep_us)
    return 0;
  int64_t remaining_us = sleep_us - idle_us;
  return (uint8_t)(((uint32_t)configured * (uint32_t)remaining_us) /
                   (uint32_t)fade_us);
}

void engine_set_brightness(int8_t level)
{
  int clamped = level;
  if (clamped < 0)
    clamped = 0;
  if (clamped > 8)
    clamped = 8;
  g_engine.brightness = (uint8_t)clamped;
  platform_display_set_contrast(engine_brightness_scale());
}

void engine_change_brightness(int8_t direction)
{
  engine_set_brightness(g_engine.brightness + direction);
  prism_settings_brightness_changed(g_engine.brightness);
}

color_t engine_led_output_color(uint8_t led)
{
  if (led >= LED_COUNT)
    return (color_t){.hex = 0};
  color_t color = g_engine.led_colors[led];
  if (wake_release_transition.active)
  {
    int64_t elapsed = platform_time_diff_us(wake_release_transition.started,
                                            platform_now_us());
    float t = elapsed <= 0 ? 0.f : elapsed >= 220000 ? 1.f
                                                   : elapsed / 220000.f;
    t = t * t * (3.f - 2.f * t);
    color = color_lerp(wake_release_transition.from[led], color, t);
  }
  uint16_t scale = engine_output_brightness_scale();
  color.r = (uint8_t)((color.r * scale + 127u) / 255u);
  color.g = (uint8_t)((color.g * scale + 127u) / 255u);
  color.b = (uint8_t)((color.b * scale + 127u) / 255u);
  return color;
}

void engine_mark_input()
{
  g_engine.last_input_at = platform_now_us();
}

static void draw_fps(u8g2_t *u8g2, uint32_t fps)
{
  static int fps_state = 0;
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawBox(u8g2, 0, 0, 14, 5);
  u8g2_SetDrawColor(u8g2, 1);
  fps_state = (fps_state + 1) % 10;
  if (fps_state < 4)
  {
    u8g2_DrawPixel(u8g2, 1, fps_state);
  }
  else
  {
    u8g2_DrawPixel(u8g2, 0, 7 - fps_state);
  }
  u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);
  char fps_str[8];
  snprintf(fps_str, sizeof(fps_str), "%d", fps);
  u8g2_DrawStr(u8g2, 3, 4, fps_str);
}

static void read_button(button_t *button, platform_time_t now, bool pressed)
{
  if (pressed)
  {
    if (!button->pressed && !button->ignore)
    {
      button->pressed = true;
      button->pressed_at = now;
      button->edge = true;
    }
  }
  else
  {
    button->ignore = false;
    if (button->pressed)
    {
      // released
      button->pressed = false;
      button->pressed_at = PLATFORM_TIME_ZERO;
      button->edge = true;
    }
  }
  if (button->edge)
  {
    engine_mark_input();
  }
}

void engine_buttons_reset()
{
  g_engine.buttons.left.edge = false;
  g_engine.buttons.left.pressed = false;
  g_engine.buttons.left.pressed_at = PLATFORM_TIME_ZERO;
  g_engine.buttons.left.ignore = true;

  g_engine.buttons.right.edge = false;
  g_engine.buttons.right.pressed = false;
  g_engine.buttons.right.pressed_at = PLATFORM_TIME_ZERO;
  g_engine.buttons.right.ignore = true;
}

static void reset_buttons(bool ignore_menu)
{
  engine_buttons_reset();

  if (ignore_menu)
  {
    g_engine.buttons.menu.edge = false;
    g_engine.buttons.menu.pressed = false;
    g_engine.buttons.menu.pressed_at = PLATFORM_TIME_ZERO;
    g_engine.buttons.menu.ignore = true;
  }
}

static void handle_menu_reset()
{
  // if the menu button is held down for a while, reset using watchdog
  platform_watchdog_update();
  if (g_engine.buttons.menu.pressed)
  {
    if (platform_time_reached(
            platform_time_add_ms(g_engine.buttons.menu.pressed_at, 5000)))
    {
      prism_settings_flush();
      // reset the system
      platform_system_reset();
      while (1)
        ;
    }
  }
}

static platform_wake_result_t engine_sleep_cycle(uint32_t quick_wake_ms)
{
  prism_settings_flush();
  platform_watchdog_disable();
  platform_display_set_enabled(false);
  platform_peripheral_set_enabled(false);
  audio_playback_set_enabled(false);
  platform_wake_result_t wake = platform_sleep_enter(quick_wake_ms);

  audio_playback_set_enabled(true);
  /* Restore the configured level before powering the OLED back up so wake
   * never exposes the last zero-contrast fade value. */
  platform_display_set_contrast(engine_brightness_scale());
  platform_display_set_enabled(true);
  platform_peripheral_set_enabled(true);
  platform_watchdog_enable(WATCHDOG_TIMEOUT_MS);

  reset_buttons(true); // ignore any edges from waking up
  engine_mark_input(); // mark input to avoid auto-sleep right after waking up
  g_engine.next_tick_at = platform_now_us();
  g_engine.next_frame_at = platform_now_us();
  return wake;
}

static void resume_after_auto_sleep(void);
static void wake_confirmation_start(platform_input_mask_t button);

void engine_enter_sleep()
{
  platform_wake_result_t wake = engine_sleep_cycle(0);
  if (wake.confirmation_required && wake.wake_button != 0)
    wake_confirmation_start(wake.wake_button);
  else
    resume_after_auto_sleep();
}

static const int32_t MENU_CONTAINER_OFFSET_CLOSED = -DISP_HEIGHT - 2;
static struct
{
  int active;
  bool ignore_release;
  struct
  {
    int32_t container;
    int32_t active;
    int32_t held;
    int32_t volume_kick;
    int32_t brightness_kick;
  } anim;
} menu_state = {
    .anim.container = MENU_CONTAINER_OFFSET_CLOSED,
};

static struct
{
  bool active;
  platform_input_mask_t button;
  uint8_t count;
  platform_time_t idle_deadline;
  platform_time_t led_transition_started;
  color_t led_from[LED_COUNT];
  color_t led_target[LED_COUNT];
  volatile int32_t circle_radius[3];
  volatile int32_t container_y;
  bool completing;
} wake_confirmation;

static void menu_action_go_home()
{
  engine_resume();
  engine_set_app(NULL);
}

static void menu_action_sleep() { engine_enter_sleep(); }

static const menu_action_t menu_actions[] = {
    {.name = "go home", .action = menu_action_go_home},
    {.name = "sleep", .action = menu_action_sleep},
    {.name = "volume", .action = NULL},
    {.name = "brightness", .action = NULL},
};
static const uint8_t MENU_ACTION_VOLUME = 2;
static const uint8_t MENU_ACTION_BRIGHTNESS = 3;

static const int32_t MENU_ACTION_COUNT =
    sizeof(menu_actions) / sizeof(menu_actions[0]);
static const int32_t MENU_ACTION_HEIGHT = 12;
static const int32_t MENU_ACTION_MARGIN = 2;

static inline int32_t menu_action_y(int index)
{
  return index * (MENU_ACTION_HEIGHT + MENU_ACTION_MARGIN);
}
static void menu_change_active(int8_t delta)
{
  menu_state.active += delta;
  if (menu_state.active < 0)
  {
    menu_state.active = MENU_ACTION_COUNT - 1;
  }
  else if (menu_state.active >= MENU_ACTION_COUNT)
  {
    menu_state.active = 0;
  }
  int32_t target_offset = menu_action_y(menu_state.active);
  anim_sys_to(&menu_state.anim.active, target_offset, 150, ANIM_EASE_OUT_CUBIC,
              NULL, NULL);
}

static void menu_enter(bool skip_animation)
{
  menu_state.anim.held = 0;
  if (skip_animation)
  {
    // skip animation, jump straight to open
    menu_state.anim.container = 0;
  }
  else
  {
    anim_sys_to(&menu_state.anim.container, 0, 300, ANIM_EASE_OUT_CUBIC, NULL,
                NULL);
  }
}

static void menu_exit()
{
  anim_sys_to(&menu_state.anim.container, MENU_CONTAINER_OFFSET_CLOSED, 300,
              ANIM_EASE_OUT_CUBIC, NULL, NULL);
}

static void menu_hide_immediate(void)
{
  anim_cancel(&menu_state.anim.container, false);
  menu_state.anim.container = MENU_CONTAINER_OFFSET_CLOSED;
}

static void resume_after_auto_sleep(void)
{
  for (uint8_t i = 0; i < 3; ++i)
    anim_cancel(&wake_confirmation.circle_radius[i], false);
  anim_cancel(&wake_confirmation.container_y, false);
  wake_confirmation.active = false;
  wake_confirmation.completing = false;
  menu_hide_immediate();
  /* App switches performed by a sleeping management command may already have
   * cleared g_engine.paused. The animation pause flag is independent, so
   * restore it unconditionally rather than using paused as a proxy. */
  anim_sys_set_paused(false);
  if (g_engine.paused)
  {
    g_engine.paused = false;
    if (g_engine.app != NULL && g_engine.app->resume != NULL)
      g_engine.app->resume();
  }
  reset_buttons(true);
  engine_mark_input();
}

void engine_wake()
{
  if (wake_confirmation.active)
    resume_after_auto_sleep();
}

static color_t wake_led_target(platform_input_mask_t button, uint8_t count,
                               uint8_t led);
static void wake_circle_pop(uint8_t index);

static void wake_confirmation_start(platform_input_mask_t button)
{
  wake_confirmation.active = true;
  wake_confirmation.button = button;
  wake_confirmation.count = 1;
  wake_confirmation.completing = false;
  wake_confirmation.container_y = 0;
  wake_confirmation.idle_deadline =
      platform_time_add_ms(platform_now_us(), 5000);
  wake_confirmation.led_transition_started = platform_now_us();
  for (uint8_t i = 0; i < LED_COUNT; ++i)
  {
    wake_confirmation.led_from[i] = (color_t){.hex = 0};
    wake_confirmation.led_target[i] = wake_led_target(button, 1, i);
  }
  for (uint8_t i = 0; i < 3; ++i)
  {
    anim_cancel(&wake_confirmation.circle_radius[i], false);
    wake_confirmation.circle_radius[i] = 5;
  }
  anim_cancel(&wake_confirmation.container_y, false);
  wake_circle_pop(0);
}

static color_t wake_led_target(platform_input_mask_t button, uint8_t count,
                               uint8_t led)
{
  bool selected = (button == PLATFORM_INPUT_LEFT && led == LED_L) ||
                  (button == PLATFORM_INPUT_RIGHT && led == LED_R);
  if (!selected)
    return (color_t){.hex = 0};
  return count >= 2 ? rgba(0, 162, 191, 255)
                    : rgba(8, 21, 89, 255);
}

static color_t wake_led_current(uint8_t led)
{
  int64_t elapsed = platform_time_diff_us(
      wake_confirmation.led_transition_started, platform_now_us());
  float t = elapsed <= 0 ? 0.f : elapsed >= 180000 ? 1.f
                                                   : elapsed / 180000.f;
  /* Smoothstep keeps the color handoff soft at both ends. */
  t = t * t * (3.f - 2.f * t);
  return color_lerp(wake_confirmation.led_from[led],
                    wake_confirmation.led_target[led], t);
}

static void wake_led_transition_to_current_stage(void)
{
  color_t current[LED_COUNT];
  for (uint8_t i = 0; i < LED_COUNT; ++i)
    current[i] = wake_led_current(i);
  wake_confirmation.led_transition_started = platform_now_us();
  for (uint8_t i = 0; i < LED_COUNT; ++i)
  {
    wake_confirmation.led_from[i] = current[i];
    wake_confirmation.led_target[i] = wake_led_target(
        wake_confirmation.button, wake_confirmation.count, i);
  }
}

static void wake_led_transition_to_success(void)
{
  color_t current[LED_COUNT];
  for (uint8_t i = 0; i < LED_COUNT; ++i)
    current[i] = wake_led_current(i);
  wake_confirmation.led_transition_started = platform_now_us();
  for (uint8_t i = 0; i < LED_COUNT; ++i)
  {
    wake_confirmation.led_from[i] = current[i];
    wake_confirmation.led_target[i] = rgba(255, 255, 255, 255);
  }
}

static void wake_release_transition_start(void)
{
  wake_release_transition.active = true;
  wake_release_transition.started = platform_now_us();
  for (uint8_t i = 0; i < LED_COUNT; ++i)
    wake_release_transition.from[i] = rgba(255, 255, 255, 255);
}

static void wake_exit_done(void *ctx)
{
  (void)ctx;
  wake_release_transition_start();
  resume_after_auto_sleep();
}

static void wake_circle_settle_done(void *ctx)
{
  uint8_t index = (uint8_t)(uintptr_t)ctx;
  if (wake_confirmation.active && wake_confirmation.completing && index == 2)
    anim_sys_to(&wake_confirmation.container_y, -DISP_HEIGHT, 300,
                ANIM_EASE_OUT_CUBIC, wake_exit_done, NULL);
}

static void wake_circle_pop(uint8_t index)
{
  anim_cancel(&wake_confirmation.circle_radius[index], false);
  wake_confirmation.circle_radius[index] = 5;
  anim_sys_to(&wake_confirmation.circle_radius[index], 10, 150,
              ANIM_EASE_OUT_CUBIC, wake_circle_settle_done,
              (void *)(uintptr_t)index);
}

static platform_input_mask_t wake_button_down(void)
{
  if (BUTTON_KEYDOWN(BUTTON_LEFT))
    return PLATFORM_INPUT_LEFT;
  if (BUTTON_KEYDOWN(BUTTON_RIGHT))
    return PLATFORM_INPUT_RIGHT;
  if (BUTTON_KEYDOWN(BUTTON_MENU))
    return PLATFORM_INPUT_MENU;
  return 0;
}

static void wake_confirmation_tick(void)
{
  if (wake_confirmation.completing)
    return;
  platform_input_mask_t pressed = wake_button_down();
  if (pressed == 0)
    return;
  if (pressed == wake_confirmation.button)
    ++wake_confirmation.count;
  else
  {
    wake_confirmation.button = pressed;
    wake_confirmation.count = 1;
    for (uint8_t i = 0; i < 3; ++i)
    {
      anim_cancel(&wake_confirmation.circle_radius[i], false);
      wake_confirmation.circle_radius[i] = 5;
    }
  }
  wake_confirmation.idle_deadline =
      platform_time_add_ms(platform_now_us(), 5000);
  if (wake_confirmation.count >= 3)
  {
    wake_confirmation.count = 3;
    wake_confirmation.completing = true;
    wake_led_transition_to_success();
    wake_circle_pop(2);
  }
  else
  {
    wake_led_transition_to_current_stage();
    wake_circle_pop(wake_confirmation.count - 1);
  }
}

static void wake_confirmation_frame(void)
{
  u8g2_t *u8g2 = platform_display_get_u8g2();
  memcpy(u8g2_GetBufferPtr(u8g2), last_app_framebuffer,
         sizeof(last_app_framebuffer));
  /* Treat the wake UI as an opaque sheet over the preserved app frame. Its
   * visible height shrinks as the sheet slides upward, revealing the app from
   * the bottom exactly like the pause menu transition. */
  int16_t overlay_bottom = DISP_HEIGHT + wake_confirmation.container_y;
  if (overlay_bottom > 0)
  {
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, 0, 0, DISP_WIDTH,
                 overlay_bottom > DISP_HEIGHT ? DISP_HEIGHT : overlay_bottom);
  }
  u8g2_SetDrawColor(u8g2, 1);
  const char *button = wake_confirmation.button == PLATFORM_INPUT_LEFT ? "L" :
                       wake_confirmation.button == PLATFORM_INPUT_RIGHT ? "R" :
                       "M";
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  for (uint8_t i = 0; i < 3; ++i)
  {
    uint8_t x = 40 + i * 24;
    int16_t y = 32 + wake_confirmation.container_y;
    uint8_t radius = (uint8_t)wake_confirmation.circle_radius[i];
    if (y + radius >= 0)
    {
      u8g2_DrawCircle(u8g2, x, y, radius, U8G2_DRAW_ALL);
      if (i < wake_confirmation.count)
        u8g2_DrawStr(u8g2, x - u8g2_GetStrWidth(u8g2, button) / 2, y + 4,
                     button);
    }
  }
  /* Virtual bottom edge: hidden at rest on row 64, then revealed as the
   * completed wake overlay slides upward. */
  u8g2_DrawHLine(u8g2, 0, DISP_HEIGHT + wake_confirmation.container_y,
                 DISP_WIDTH);

  for (uint8_t i = 0; i < LED_COUNT; ++i)
    g_engine.led_colors[i] = wake_led_current(i);
}

static void engine_enter_auto_sleep(void)
{
  engine_pause(true);
  platform_wake_result_t wake = engine_sleep_cycle(2000);
  if (wake.confirmation_required && wake.wake_button != 0)
    wake_confirmation_start(wake.wake_button);
  else
    resume_after_auto_sleep();
}

static void wake_confirmation_timeout(void)
{
  platform_wake_result_t wake = engine_sleep_cycle(0);
  if (wake.confirmation_required && wake.wake_button != 0)
    wake_confirmation_start(wake.wake_button);
  else
    resume_after_auto_sleep();
}

static void menu_frame()
{
  // always handle menu button
  if (BUTTON_KEYDOWN(BUTTON_MENU))
  {
    if (g_engine.paused)
    {
      // close menu
      engine_resume();
    }
    else
    {
      // open menu
      engine_pause(false);
    }
  }

  // only handle other buttons if menu is open
  if (g_engine.paused)
  {
    button_id_t button_id = engine_button_get_pressed_first();
    if (button_id != BUTTON_NONE)
    {
      float held = engine_button_held_ratio(button_id);
      menu_state.ignore_release = held > 0.f;
      if (menu_state.active == MENU_ACTION_VOLUME ||
          menu_state.active == MENU_ACTION_BRIGHTNESS)
      {
        // change level repeatedly when held
        if (held >= .2f)
        {
          static platform_time_t last_change = 0;
          if (platform_time_reached(platform_time_add_ms(last_change, 200)))
          {
            last_change = platform_now_us();
            if (button_id == BUTTON_LEFT)
            {
              if (menu_state.active == MENU_ACTION_VOLUME)
              {
                engine_change_volume(-1);
                menu_state.anim.volume_kick = -2;
              }
              else
              {
                engine_change_brightness(-1);
                menu_state.anim.brightness_kick = -2;
              }
            }
            else
            {
              if (menu_state.active == MENU_ACTION_VOLUME)
              {
                engine_change_volume(1);
                menu_state.anim.volume_kick = 2;
              }
              else
              {
                engine_change_brightness(1);
                menu_state.anim.brightness_kick = 2;
              }
            }
            volatile int32_t *kick = menu_state.active == MENU_ACTION_VOLUME
                                         ? &menu_state.anim.volume_kick
                                         : &menu_state.anim.brightness_kick;
            anim_sys_to(kick, 0, 150,
                        ANIM_EASE_OUT_CUBIC, NULL, NULL);
          }
        }
      }
      else
      {
        // once held enough, trigger menu action
        if (held > 0)
        {
          anim_cancel(&menu_state.anim.held, false);
          menu_state.anim.held = 14 * ease_out_cubic(held);
        }
        if (held >= 1.f)
        {
          if (menu_actions[menu_state.active].action)
          {
            menu_actions[menu_state.active].action();
          }
          menu_state.anim.held = 0;
        }
      }
    }
    if (BUTTON_KEYUP(BUTTON_LEFT))
    {
      anim_sys_to(&menu_state.anim.held, 0, 150, ANIM_EASE_OUT_CUBIC, NULL,
                  NULL);
      if (!menu_state.ignore_release)
        menu_change_active(-1);
      menu_state.ignore_release = false;
    }

    if (BUTTON_KEYUP(BUTTON_RIGHT))
    {
      anim_sys_to(&menu_state.anim.held, 0, 150, ANIM_EASE_OUT_CUBIC, NULL,
                  NULL);
      if (!menu_state.ignore_release)
        menu_change_active(1);
      menu_state.ignore_release = false;
    }
  }

  /* A manual sleep action may have blocked inside menu_frame() until a wake
   * button arrived. Do not render the old pause menu for that return frame. */
  if (wake_confirmation.active)
    return;

  if (menu_state.anim.container == MENU_CONTAINER_OFFSET_CLOSED)
  {
    // menu closed, we can skip drawing since it will all be off-screen anyway
    return;
  }

  u8g2_t *u8g2 = platform_display_get_u8g2();
  vec2_t pos = vec2(0, menu_state.anim.container);

  elm_t root = elm_root(u8g2, pos);
  // draw background
  u8g2_SetDrawColor(u8g2, 0);
  elm_box(&root, vec2(0, 0), DISP_WIDTH, DISP_HEIGHT + 1);
  u8g2_SetDrawColor(u8g2, 1);
  elm_hline(&root, vec2(0, DISP_HEIGHT + 1), DISP_WIDTH);

  // draw menu items
  elm_t items = elm_child(&root, vec2(0, 8));
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  for (uint8_t i = 0; i < MENU_ACTION_COUNT; i++)
  {
    vec2_t item_pos = vec2(0, menu_action_y(i));
    elm_t item = elm_child(&items, item_pos);
    uint16_t item_text_x = 5;
    if (i == MENU_ACTION_VOLUME || i == MENU_ACTION_BRIGHTNESS)
    {
      // level item, render the bubbles
      elm_t right_edge = elm_child(&item, vec2(DISP_WIDTH - 5, 0));
      uint8_t level = i == MENU_ACTION_VOLUME ? g_engine.volume
                                               : g_engine.brightness;
      for (uint8_t j = 0; j < 8; j++)
      {
        uint8_t tick = 7 - j;
        elm_t tick_tl = elm_child(&right_edge, vec2(-3 - (j * 5), 3));
        if (tick < level)
        {
          elm_rounded_box(&tick_tl, VEC2_Z, 4, 6, 1);
        }
        else
        {
          elm_rounded_frame(&tick_tl, VEC2_Z, 4, 6, 1);
        }
      }
      item_text_x += i == MENU_ACTION_VOLUME ? menu_state.anim.volume_kick
                                              : menu_state.anim.brightness_kick;
    }
    else
    {
      if (i == menu_state.active)
      {
        item_text_x += menu_state.anim.held;
        elm_hline(&item, vec2(5, 6), MAX(menu_state.anim.held - 3, 0));
      }
    }
    elm_str(&item, vec2(item_text_x, 9), menu_actions[i].name);
  }

  elm_rounded_frame(&items, vec2(0, menu_state.anim.active), DISP_WIDTH,
                    MENU_ACTION_HEIGHT, 3);

  // draw hud
  u8g2_SetDrawColor(u8g2, 0);
  elm_box(&root, vec2(0, 0), DISP_WIDTH, 8);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  elm_str(&root, vec2(0, 6), g_engine.app->name);

  char chg_str[8];
  platform_power_state_t power_state = platform_peripheral_get_power_state();
  if (power_state.charging)
  {
    snprintf(chg_str, sizeof(chg_str), "CHG");
  }
  else if (power_state.plugged_in)
  {
    snprintf(chg_str, sizeof(chg_str), "USB");
  }
  else
  {
    snprintf(chg_str, sizeof(chg_str), "%d", power_state.battery_level);
  }
  elm_str(&root, vec2(DISP_WIDTH - u8g2_GetStrWidth(u8g2, chg_str), 6),
          chg_str);
}

static inline void engine_do_tick()
{
  anim_tick(); // always tick animations
  if (wake_confirmation.active)
  {
    wake_confirmation_tick();
    /* Wake confirmation runs at the 1 kHz tick rate, while ordinary edges
     * are normally retired by the 120 Hz frame loop. Consume them here so a
     * single physical press can never be counted more than once. */
    g_engine.buttons.left.edge = false;
    g_engine.buttons.right.edge = false;
    g_engine.buttons.menu.edge = false;
  }
  else if (!g_engine.paused)
  {
    // advance app if not paused
    if (g_engine.app->tick != NULL)
    {
      g_engine.app->tick();
      // reset button edge. if no tick(), they will instead be reset next
      // frame
      g_engine.buttons.left.edge = false;
      g_engine.buttons.right.edge = false;
    }
    g_engine.tick++;
  }

  if (g_engine.on_tick_cb != NULL)
  {
    g_engine.on_tick_cb();
  }
}

static inline void engine_do_frame()
{
  for (uint8_t i = 0; i < LED_COUNT; i++)
  {
    g_engine.led_colors[i] = (color_t){.hex = 0x000000};
  }
  if (wake_confirmation.active &&
      platform_time_reached(wake_confirmation.idle_deadline))
    wake_confirmation_timeout();

  if (wake_confirmation.active)
  {
    wake_confirmation_frame();
  }
  else if (!g_engine.paused)
  {
    // draw screen buffer
    u8g2_t *u8g2 = platform_display_get_u8g2();
    u8g2_ClearBuffer(u8g2);
    if (g_engine.app->frame != NULL)
      g_engine.app->frame();
    memcpy(last_app_framebuffer, u8g2_GetBufferPtr(u8g2),
           sizeof(last_app_framebuffer));
  }

  if (!wake_confirmation.active)
  {
    menu_frame();
    prism_settings_frame();
  }
  prism_settings_task();
  prism_cartridge_persistence_task();

  if (g_engine.on_frame_cb != NULL)
  {
    g_engine.on_frame_cb();
  }

  // Apply the final OS-owned idle fade after the app and menu have rendered.
  platform_display_set_contrast(engine_output_brightness_scale());
  // write display
  u8g2_SendBuffer(platform_display_get_u8g2());
  // write LEDs
  color_t output_leds[LED_COUNT];
  for (uint8_t i = 0; i < LED_COUNT; ++i)
    output_leds[i] = engine_led_output_color(i);
  platform_leds_show(output_leds, LED_COUNT);
  if (wake_release_transition.active &&
      platform_time_diff_us(wake_release_transition.started,
                            platform_now_us()) >= 220000)
    wake_release_transition.active = false;

  // reset button edges so they only last for a single frame at most
  g_engine.buttons.left.edge = false;
  g_engine.buttons.right.edge = false;
  g_engine.buttons.menu.edge = false;

  // check for auto sleep
  if (!wake_confirmation.active && platform_time_reached(
          platform_time_add_ms(g_engine.last_input_at, AUTO_SLEEP_TIMEOUT_MS)))
  {
    engine_enter_auto_sleep();
  }
}

void engine_run_forever()
{
  platform_display_set_enabled(true);
  platform_peripheral_set_enabled(true);
  platform_task();

  TimingInstrumenter ti_tick;
  TimingInstrumenter ti_frame;
  ti_init(&ti_tick);
  ti_init(&ti_frame);

  uint64_t last_frame_us = 0;
  platform_time_t last_log_us = platform_now_us();
  uint32_t last_log_frames = 0;
  uint32_t fps = 0;

  u8g2_t *u8g2 = platform_display_get_u8g2();

  uint32_t dt = 0;
  platform_watchdog_enable(WATCHDOG_TIMEOUT_MS);
  g_engine.now = PLATFORM_TIME_ZERO;
  g_engine.tick = 0;

  g_engine.next_tick_at = platform_now_us();
  g_engine.next_frame_at = platform_now_us();

  while (1)
  {
    platform_watchdog_update();
    platform_task();

    // we enter when either a frame or a tick is due.
    // first, fast-forward ticks until we're caught up.
    platform_time_t now = platform_now_us();
    int ticks_processed = 0;
    while (now >= g_engine.next_tick_at)
    {
      ti_start(&ti_tick);

      // tick!
      g_engine.now = g_engine.next_tick_at;

      if (ticks_processed == 0)
      {
        // only do this once per batch of ticks, since inputs cant change between
        // fast-forwarded ticks.
        // read inputs
        platform_input_mask_t input_mask = platform_input_read_mask();
        read_button(&g_engine.buttons.left, g_engine.now, (input_mask & PLATFORM_INPUT_LEFT) != 0);
        read_button(&g_engine.buttons.right, g_engine.now, (input_mask & PLATFORM_INPUT_RIGHT) != 0);
        read_button(&g_engine.buttons.menu, g_engine.now, (input_mask & PLATFORM_INPUT_MENU) != 0);

        handle_menu_reset();
      }

      engine_do_tick();

      if (ticks_processed++ % 50 == 0)
      {
        // for long updates, keep watchdog happy
        platform_watchdog_update();
      }

      // advance to next tick.
      g_engine.next_tick_at += TICK_INTERVAL_US;
      now = platform_now_us();
      ti_stop(&ti_tick);
    }

    // then, if a frame is due, draw it. note that we dont catch-up frames.
    now = platform_now_us();
    if (platform_time_reached(g_engine.next_frame_at))
    {
      ti_start(&ti_frame);
      // frame!
      engine_do_frame();

      // schedule next frame
      int64_t frame_time = platform_now_us() - now;
      g_engine.next_frame_at = platform_now_us() + TARGET_FRAME_INTERVAL_US - frame_time;
      ti_stop(&ti_frame);
    }

    // log fps
    now = platform_now_us();
    int32_t log_time = platform_time_diff_us(last_log_us, now);
    if (log_time > 1000000)
    {
      fps = (uint32_t)(ti_frame.count / (log_time / 1000000.0f));
      uint32_t tps = (uint32_t)(ti_tick.count / (log_time / 1000000.0f));

      float ti_tick_avg = ti_get_average_ms(&ti_tick, true);
      float ti_frame_avg = ti_get_average_ms(&ti_frame, true);
      printf(
          "fps: %d | tps: %d | frame: %.2f / %.2f ms | tick: %.2f / %.2f ms\n", fps, tps,
          ti_frame_avg, TARGET_FRAME_INTERVAL_US / 1000.0f, ti_tick_avg, TICK_INTERVAL_US / 1000.0f);

      // extern char __StackLimit;
      // char *heap = sbrk(0);
      // printf("ram free: %.2f kb", ((float)(&__StackLimit - heap)) / 1024.0f);

      last_log_us = now;
    }

    // sleep until the next deadline
    platform_time_t next_deadline = MIN(g_engine.next_tick_at, g_engine.next_frame_at);
    int64_t sleep_time = platform_time_diff_us(platform_now_us(), next_deadline);
    if (sleep_time > 0)
      platform_sleep_us(sleep_time);
  }
}

void engine_set_app(app_t *app)
{
  if (wake_confirmation.active)
  {
    wake_confirmation.active = false;
    g_engine.paused = false;
    anim_sys_set_paused(false);
    menu_hide_immediate();
  }
  if (g_engine.app != NULL && g_engine.app->leave != NULL)
  {
    g_engine.app->leave();
  }
  anim_sys_clear_all();
  reset_buttons(false);

  if (app == NULL)
  {
    app = &app_launcher;
  }

  g_engine.tick = 0;
  g_engine.app = app;
  g_engine.paused = false;
  anim_sys_set_paused(false);

  // reset audio synth
  audio_synth_panic(&g_engine.synth);
  audio_synth_reset_voices(&g_engine.synth);

  // seed based on time
  srand((unsigned int)platform_now_us());

  if (g_engine.app != NULL && g_engine.app->enter != NULL)
  {
    g_engine.app->enter();
  }
}

void engine_pause(bool skip_animation)
{
  if (g_engine.paused)
    return;
  reset_buttons(false);
  g_engine.paused = true;
  anim_sys_set_paused(true);
  if (g_engine.app != NULL && g_engine.app->pause != NULL)
  {
    g_engine.app->pause();
  }
  menu_enter(skip_animation);
}

void engine_resume()
{
  if (!g_engine.paused)
    return;
  reset_buttons(false);
  g_engine.paused = false;
  anim_sys_set_paused(false);
  if (g_engine.app != NULL && g_engine.app->resume != NULL)
  {
    g_engine.app->resume();
  }
  menu_exit();
}
