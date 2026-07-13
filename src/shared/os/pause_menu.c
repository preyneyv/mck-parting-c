#include "pause_menu.h"

#include <platform/display.h>
#include <platform/peripheral.h>
#include <platform/time.h>
#include <prism/graphics/layout.h>
#include <shared/anim.h>
#include <shared/config.h>
#include <shared/engine.h>
#include <shared/os/power_indicator.h>
#include <shared/os/system_sound.h>
#include <shared/utils/misc.h>

#include "engine_internal.h"
#include "wake_confirmation.h"

enum
{
  MENU_GO_HOME,
  MENU_SLEEP,
  MENU_VOLUME,
  MENU_BRIGHTNESS,
  MENU_ACTION_COUNT,
  MENU_ACTION_HEIGHT = 12,
  MENU_ACTION_MARGIN = 2,
  MENU_CLOSED_Y = -DISP_HEIGHT - 2,
};

static struct
{
  int active;
  bool ignore_release;
  uint8_t hold_sound_step;
  platform_time_t last_level_change;
  struct
  {
    int32_t container;
    int32_t active;
    int32_t held;
    int32_t volume_kick;
    int32_t brightness_kick;
  } anim;
} menu = {
    .anim.container = MENU_CLOSED_Y,
};

static void go_home(void)
{
  engine_resume();
  engine_set_app(NULL);
}

static const menu_action_t actions[MENU_ACTION_COUNT] = {
    [MENU_GO_HOME] = {.name = "go home", .action = go_home},
    [MENU_SLEEP] = {.name = "sleep", .action = engine_enter_sleep},
    [MENU_VOLUME] = {.name = "volume"},
    [MENU_BRIGHTNESS] = {.name = "brightness"},
};

static int32_t action_y(int index)
{
  return index * (MENU_ACTION_HEIGHT + MENU_ACTION_MARGIN);
}

static void change_active(int8_t delta)
{
  menu.active += delta;
  if (menu.active < 0)
    menu.active = MENU_ACTION_COUNT - 1;
  else if (menu.active >= MENU_ACTION_COUNT)
    menu.active = 0;
  anim_sys_to(&menu.anim.active, action_y(menu.active), 150,
              ANIM_EASE_OUT_CUBIC, NULL, NULL);
  system_sound_navigation();
}

void pause_menu_show(bool immediate)
{
  menu.hold_sound_step = 0;
  menu.anim.held = 0;
  if (immediate)
    menu.anim.container = 0;
  else
    anim_sys_to(&menu.anim.container, 0, 300, ANIM_EASE_OUT_CUBIC, NULL,
                NULL);
}

void pause_menu_hide(void)
{
  anim_sys_to(&menu.anim.container, MENU_CLOSED_Y, 300,
              ANIM_EASE_OUT_CUBIC, NULL, NULL);
}

void pause_menu_hide_immediate(void)
{
  anim_cancel(&menu.anim.container, false);
  menu.anim.container = MENU_CLOSED_Y;
  menu.hold_sound_step = 0;
}

static void adjust_level(button_id_t button)
{
  int8_t direction = button == BUTTON_LEFT ? -1 : 1;
  volatile int32_t *kick;
  if (menu.active == MENU_VOLUME)
  {
    engine_change_volume(direction);
    kick = &menu.anim.volume_kick;
  }
  else
  {
    engine_change_brightness(direction);
    kick = &menu.anim.brightness_kick;
  }
  *kick = direction * 2;
  anim_sys_to(kick, 0, 150, ANIM_EASE_OUT_CUBIC, NULL, NULL);
  system_sound_navigation();
}

static void handle_input(void)
{
  if (BUTTON_KEYDOWN(BUTTON_MENU))
  {
    bool opening = !engine_is_paused();
    system_sound_menu(opening);
    if (!opening)
      engine_resume();
    else
      engine_pause(false);
  }

  if (!engine_is_paused())
    return;

  button_id_t button = engine_button_get_pressed_first();
  if (button != BUTTON_NONE)
  {
    float held = engine_button_held_ratio(button);
    menu.ignore_release = held > 0.f;
    if (menu.active == MENU_VOLUME || menu.active == MENU_BRIGHTNESS)
    {
      if (held >= .2f &&
          platform_time_reached(
              platform_time_add_ms(menu.last_level_change, 200)))
      {
        menu.last_level_change = platform_now_us();
        adjust_level(button);
      }
    }
    else
    {
      if (held > 0.f)
      {
        uint8_t sound_step = (uint8_t)(held * 5.f) + 1u;
        if (sound_step > 5u)
          sound_step = 5u;
        if (sound_step != menu.hold_sound_step)
        {
          menu.hold_sound_step = sound_step;
          system_sound_hold(sound_step);
        }
        anim_cancel(&menu.anim.held, false);
        menu.anim.held = 14 * ease_out_cubic(held);
      }
      if (held >= 1.f)
      {
        if (actions[menu.active].action != NULL)
          actions[menu.active].action();
        menu.anim.held = 0;
      }
    }
  }

  if (BUTTON_KEYUP(BUTTON_LEFT))
  {
    menu.hold_sound_step = 0;
    anim_sys_to(&menu.anim.held, 0, 150, ANIM_EASE_OUT_CUBIC, NULL, NULL);
    if (!menu.ignore_release)
      change_active(-1);
    menu.ignore_release = false;
  }
  if (BUTTON_KEYUP(BUTTON_RIGHT))
  {
    menu.hold_sound_step = 0;
    anim_sys_to(&menu.anim.held, 0, 150, ANIM_EASE_OUT_CUBIC, NULL, NULL);
    if (!menu.ignore_release)
      change_active(1);
    menu.ignore_release = false;
  }
}

static void draw(void)
{
  if (menu.anim.container == MENU_CLOSED_Y)
    return;

  u8g2_t *u8g2 = platform_display_get_u8g2();
  elm_t root = elm_root(u8g2, vec2(0, menu.anim.container));
  u8g2_SetDrawColor(u8g2, 0);
  elm_box(&root, VEC2_Z, DISP_WIDTH, DISP_HEIGHT + 1);
  u8g2_SetDrawColor(u8g2, 1);
  elm_hline(&root, vec2(0, DISP_HEIGHT + 1), DISP_WIDTH);

  elm_t items = elm_child(&root, vec2(0, 8));
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  for (uint8_t i = 0; i < MENU_ACTION_COUNT; ++i)
  {
    elm_t item = elm_child(&items, vec2(0, action_y(i)));
    uint16_t text_x = 5;
    if (i == MENU_VOLUME || i == MENU_BRIGHTNESS)
    {
      elm_t right = elm_child(&item, vec2(DISP_WIDTH - 5, 0));
      uint8_t level = i == MENU_VOLUME ? engine_volume()
                                       : engine_brightness();
      for (uint8_t j = 0; j < 8; ++j)
      {
        elm_t tick = elm_child(&right, vec2(-3 - (j * 5), 3));
        if (7 - j < level)
          elm_rounded_box(&tick, VEC2_Z, 4, 6, 1);
        else
          elm_rounded_frame(&tick, VEC2_Z, 4, 6, 1);
      }
      text_x += i == MENU_VOLUME ? menu.anim.volume_kick
                                  : menu.anim.brightness_kick;
    }
    else if (i == menu.active)
    {
      text_x += menu.anim.held;
      elm_hline(&item, vec2(5, 6), MAX(menu.anim.held - 3, 0));
    }
    elm_str(&item, vec2(text_x, 9), actions[i].name);
  }

  elm_rounded_frame(&items, vec2(0, menu.anim.active), DISP_WIDTH,
                    MENU_ACTION_HEIGHT, 3);

  u8g2_SetDrawColor(u8g2, 0);
  elm_box(&root, VEC2_Z, DISP_WIDTH, 8);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  elm_str(&root, vec2(0, 6), engine_app_name());

  platform_power_state_t power = platform_peripheral_get_power_state();
  power_indicator_draw(u8g2, root.pos.x + DISP_WIDTH, root.pos.y, power);
}

void pause_menu_frame(void)
{
  handle_input();
  if (!wake_confirmation_active())
    draw();
}
