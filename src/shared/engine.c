#include <stdio.h>

#include <hardware/divider.h>
#include <hardware/watchdog.h>

#include <shared/utils/elm.h>
#include <shared/utils/timing.h>
#include <shared/utils/vec.h>

#include "anim.h"
#include "apps/apps.h"
#include "engine.h"

// #define DEBUG_FPS

// global engine instance
engine_t g_engine;

void engine_init() {
  // initialize all subsystems
  audio_synth_init(&g_engine.synth, AUDIO_SAMPLE_RATE, 1000);
  g_engine.synth.master_level = q1x15_f(.5f); // todo: volume control

  display_init(&g_engine.display);
  peripheral_init(&g_engine.peripheral);
  leds_init(&g_engine.leds);

  g_engine.buttons.left.id = BUTTON_LEFT;
  g_engine.buttons.right.id = BUTTON_RIGHT;
  g_engine.buttons.menu.id = BUTTON_MENU;
  engine_buttons_init(&g_engine.buttons);

  engine_set_app(NULL);
}

static void draw_fps(u8g2_t *u8g2, uint32_t fps) {
  static int fps_state = 0;
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawBox(u8g2, 0, 0, 14, 5);
  u8g2_SetDrawColor(u8g2, 1);
  fps_state = (fps_state + 1) % 10;
  if (fps_state < 4) {
    u8g2_DrawPixel(u8g2, 1, fps_state);
  } else {
    u8g2_DrawPixel(u8g2, 0, 7 - fps_state);
  }
  u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tf);
  char fps_str[8];
  snprintf(fps_str, sizeof(fps_str), "%d", fps);
  u8g2_DrawStr(u8g2, 3, 4, fps_str);
}

static void read_button(button_t *button, absolute_time_t now) {
  button->edge = false;
  bool pressed = engine_button_read(button->id);
  if (pressed) {
    if (!button->pressed && !button->ignore) {
      button->pressed = true;
      button->pressed_at = now;
      button->edge = true;
    }
  } else {
    button->ignore = false;
    if (button->pressed) {
      // released
      button->pressed = false;
      button->pressed_at = nil_time;
      button->edge = true;
    }
  }
}

static void reset_buttons() {
  g_engine.buttons.left.edge = false;
  g_engine.buttons.left.pressed = false;
  g_engine.buttons.left.pressed_at = nil_time;
  g_engine.buttons.left.ignore = true;

  g_engine.buttons.right.edge = false;
  g_engine.buttons.right.pressed = false;
  g_engine.buttons.right.pressed_at = nil_time;
  g_engine.buttons.right.ignore = true;

  // g_engine.buttons.menu.edge = false;
  // g_engine.buttons.menu.pressed = false;
  // g_engine.buttons.menu.pressed_at = nil_time;
  // g_engine.buttons.menu.ignore = true;
}

static void handle_menu_reset() {
  // if the menu button is held down for a while, reset using watchdog
  watchdog_update();
  if (g_engine.buttons.menu.pressed) {
    if (time_reached(delayed_by_ms(g_engine.buttons.menu.pressed_at, 5000))) {
      // reset the system
      watchdog_enable(0, 0);
      while (1)
        ;
    }
  }
}

void engine_enter_sleep() {
  watchdog_disable();
  display_set_enabled(&g_engine.display, false);
  peripheral_set_enabled(&g_engine.peripheral, false);
  audio_playback_set_enabled(false);

  engine_sleep_until_interrupt();

  audio_playback_set_enabled(true);
  display_set_enabled(&g_engine.display, true);
  peripheral_set_enabled(&g_engine.peripheral, true);
  watchdog_enable(200, 1);
}

static const int32_t MENU_CONTAINER_OFFSET_CLOSED = -DISP_HEIGHT - 1;
static struct {
  int active;
  int32_t container_offset;
  int32_t active_offset;
  bool ignore_right_release;
  bool ignore_left_release;
} menu_state = {
    .container_offset = MENU_CONTAINER_OFFSET_CLOSED,
};

static const menu_action_t menu_actions[] = {
    {.name = "go home", .action = NULL},
    {.name = "sleep", .action = NULL},
    {.name = "volume", .action = NULL},
};

static const int32_t MENU_ACTION_COUNT =
    sizeof(menu_actions) / sizeof(menu_actions[0]);
static const int32_t MENU_ACTION_HEIGHT = 18;
static const int32_t MENU_ACTION_MARGIN = 0;

static inline int32_t menu_action_y(int index) {
  return index * (MENU_ACTION_HEIGHT + MENU_ACTION_MARGIN);
}
static void menu_change_active(int8_t delta) {
  menu_state.active += delta;
  if (menu_state.active < 0) {
    menu_state.active = MENU_ACTION_COUNT - 1;
  } else if (menu_state.active >= MENU_ACTION_COUNT) {
    menu_state.active = 0;
  }
  int32_t target_offset = menu_action_y(menu_state.active);
  anim_sys_to(&menu_state.active_offset, target_offset, 150,
              ANIM_EASE_OUT_CUBIC, NULL, NULL);
}

static void menu_frame() {
  u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);

  if (BUTTON_KEYDOWN(BUTTON_MENU)) {
    if (g_engine.paused) {
      // close menu
      engine_resume();
      anim_sys_to(&menu_state.container_offset, MENU_CONTAINER_OFFSET_CLOSED,
                  300, ANIM_EASE_OUT_CUBIC, NULL, NULL);

    } else {
      // open menu
      engine_pause();
      anim_sys_to(&menu_state.container_offset, 0, 300, ANIM_EASE_OUT_CUBIC,
                  NULL, NULL);
    }
  }

  if (g_engine.paused) {
    // handle button events
    if (BUTTON_KEYUP(BUTTON_LEFT)) {
      menu_change_active(-1);
    }
    if (BUTTON_KEYUP(BUTTON_RIGHT)) {
      menu_change_active(1);
    }
  }

  if (menu_state.container_offset == MENU_CONTAINER_OFFSET_CLOSED) {
    // menu closed, nothing to do
    return;
  }
  vec2_t pos = vec2(0, menu_state.container_offset);

  elm_t root = elm_root(u8g2, pos);
  // draw background
  u8g2_SetDrawColor(u8g2, 0);
  elm_box(&root, vec2(0, 0), DISP_WIDTH, DISP_HEIGHT);
  u8g2_SetDrawColor(u8g2, 1);
  elm_hline(&root, vec2(0, DISP_HEIGHT), DISP_WIDTH);

  // draw menu items
  elm_t items = elm_child(&root, vec2(0, 10));
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  for (uint8_t i = 0; i < MENU_ACTION_COUNT; i++) {
    vec2_t item_pos = vec2(0, menu_action_y(i));
    elm_t item = elm_child(&items, item_pos);
    elm_str(&item, vec2(5, 12), menu_actions[i].name);
  }

  elm_rounded_frame(&items, vec2(0, menu_state.active_offset), DISP_WIDTH,
                    MENU_ACTION_HEIGHT, 3);

  // draw hud
  u8g2_SetDrawColor(u8g2, 0);
  elm_box(&root, vec2(0, 0), DISP_WIDTH, 8);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  elm_str(&root, vec2(0, 6), g_engine.app->name);

  char chg_str[8] = "CHG\0";
  if (!g_engine.peripheral.charging) {
    snprintf(chg_str, sizeof(chg_str), "%d", g_engine.peripheral.battery_level);
  }
  elm_str(&root, vec2(DISP_WIDTH - u8g2_GetStrWidth(u8g2, chg_str), 6),
          chg_str);
}

void engine_run_forever() {
  const uint32_t UPDATE_PERIPHERAL_EVERY = 120; // frames
  display_set_enabled(&g_engine.display, true);
  peripheral_set_enabled(&g_engine.peripheral, true);
  peripheral_read_inputs(&g_engine.peripheral);

  TimingInstrumenter ti_tick;
  TimingInstrumenter ti_show;
  ti_init(&ti_tick);
  ti_init(&ti_show);

  uint64_t last_frame_us = 0;
  absolute_time_t last_log_us = get_absolute_time();
  uint32_t last_log_frames = 0;
  uint32_t fps = 0;

  u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);

  uint32_t dt = 0;
  uint32_t peripheral_update_counter = 0;
  watchdog_enable(200, 1);
  g_engine.now = get_absolute_time();
  g_engine.tick = 0;
  while (1) {
    absolute_time_t now = get_absolute_time();
    dt = ((uint32_t)absolute_time_diff_us(g_engine.now, now)) + dt;
    divmod_result_t res = hw_divider_divmod_u32(dt, TICK_INTERVAL_US);
    uint32_t ticks = to_quotient_u32(res);
    dt = to_remainder_u32(res);
    g_engine.now = now;

    // update buttons
    read_button(&g_engine.buttons.left, now);
    read_button(&g_engine.buttons.right, now);
    read_button(&g_engine.buttons.menu, now);

    handle_menu_reset();

    peripheral_update_counter++;
    if (peripheral_update_counter >= UPDATE_PERIPHERAL_EVERY) {
      peripheral_read_inputs(&g_engine.peripheral);
      peripheral_update_counter = 0;
    }

    ti_start(&ti_tick);

    while (ticks--) {
      anim_tick(); // always tick animations

      if (!g_engine.paused) {
        // advance app if not paused
        if (g_engine.app->tick != NULL) {
          g_engine.app->tick();
          // reset button edge. if no tick(), they will instead be reset next
          // frame
          g_engine.buttons.left.edge = false;
          g_engine.buttons.right.edge = false;
        }
        g_engine.tick++; // todo: this should technically be part of the app,
                         // not the engine
      }
    }

    if (!g_engine.paused) {
      // draw screen buffer
      u8g2_ClearBuffer(u8g2);
      if (g_engine.app->frame != NULL)
        g_engine.app->frame();
    }

    menu_frame();

    ti_stop(&ti_tick);
#ifdef DEBUG_FPS
    draw_fps(u8g2, fps);
#endif

    ti_start(&ti_show);
    // write display
    u8g2_SendBuffer(u8g2);
    // write LEDs
    leds_show(&g_engine.leds);
    ti_stop(&ti_show);

    // log fps and frame limit
    last_log_frames++;
    if (absolute_time_diff_us(last_log_us, now) > 1000000) {
      fps = last_log_frames;
      float ti_tick_avg = ti_get_average_ms(&ti_tick, true);
      float ti_show_avg = ti_get_average_ms(&ti_show, true);
      float ti_frame_avg = ti_tick_avg + ti_show_avg;
      printf(
          "fps: %d | frame: %.2f / %.2f ms | tick: %.2f ms | show: %.2f ms\n",
          fps, ti_frame_avg, TARGET_FRAME_INTERVAL_US / 1000.0f, ti_tick_avg,
          ti_show_avg);
      last_log_us = now;
      last_log_frames = 0;
    }

    now = get_absolute_time(); // update timestamp for frame limit calc
    uint64_t spent_us = absolute_time_diff_us(last_frame_us, now);
    if (spent_us < TARGET_FRAME_INTERVAL_US) {
      sleep_us(TARGET_FRAME_INTERVAL_US - spent_us);
      last_frame_us = get_absolute_time();
    } else {
      last_frame_us = now;
    }
  }
}

void engine_set_app(app_t *app) {
  if (g_engine.app != NULL && g_engine.app->leave != NULL) {
    g_engine.app->leave();
  }
  anim_sys_clear_all();
  reset_buttons();

  if (app == NULL) {
    app = &app_launcher;
  }

  g_engine.tick = 0;
  g_engine.app = app;
  g_engine.paused = false;

  if (g_engine.app != NULL && g_engine.app->enter != NULL) {
    g_engine.app->enter();
  }
}

void engine_pause() {
  reset_buttons();
  g_engine.paused = true;
  anim_sys_set_paused(true);
  if (g_engine.app != NULL && g_engine.app->pause != NULL) {
    g_engine.app->pause();
  }
}

void engine_resume() {
  reset_buttons();
  g_engine.paused = false;
  anim_sys_set_paused(false);
  if (g_engine.app != NULL && g_engine.app->resume != NULL) {
    g_engine.app->resume();
  }
}
