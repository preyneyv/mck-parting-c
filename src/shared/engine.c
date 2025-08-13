#include <stdio.h>

#include <hardware/divider.h>
#include <hardware/watchdog.h>

#include <shared/utils/timing.h>

#include "apps/apps.h"
#include "engine.h"

#define DEBUG_FPS

// global engine instance
engine_t engine;

void engine_init() {
  // initialize all subsystems
  audio_synth_init(&engine.synth, AUDIO_SAMPLE_RATE, 1000);
  engine.synth.master_level = q1x15_f(.5f); // todo: volume control

  display_init(&engine.display);
  peripheral_init(&engine.peripheral);
  leds_init(&engine.leds);

  engine.buttons.left.id = BUTTON_LEFT;
  engine.buttons.right.id = BUTTON_RIGHT;
  engine.buttons.menu.id = BUTTON_MENU;
  engine_buttons_init(&engine.buttons);

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
    if (!button->pressed) {
      button->pressed = true;
      button->pressed_at = now;
      button->edge = true;
    }
  } else {
    if (button->pressed) {
      // released
      button->pressed = false;
      button->pressed_at = nil_time;
      button->edge = true;
    }
  }
}

static void handle_menu_reset() {
  // if the menu button is held down for a while, reset using watchdog
  watchdog_update();
  if (engine.buttons.menu.pressed) {
    if (time_reached(delayed_by_ms(engine.buttons.menu.pressed_at, 5000))) {
      // reset the system
      watchdog_enable(0, 0);
      while (1)
        ;
    }
  }
}

void engine_enter_sleep() {
  watchdog_disable();
  display_set_enabled(&engine.display, false);
  peripheral_set_enabled(&engine.peripheral, false);
  audio_playback_set_enabled(false);

  // while (true)
  //   sleep_ms(1000);

  engine_sleep_until_interrupt();

  audio_playback_set_enabled(true);
  display_set_enabled(&engine.display, true);
  peripheral_set_enabled(&engine.peripheral, true);
  watchdog_enable(200, 1);
}

void engine_run_forever() {
  const uint32_t UPDATE_PERIPHERAL_EVERY = 1000; // ticks
  display_set_enabled(&engine.display, true);
  peripheral_set_enabled(&engine.peripheral, true);

  TimingInstrumenter ti_tick;
  TimingInstrumenter ti_show;
  ti_init(&ti_tick);
  ti_init(&ti_show);

  uint64_t last_frame_us = 0;
  uint64_t last_log_us = 0;
  uint32_t last_log_frames = 0;
  uint32_t fps = 0;

  u8g2_t *u8g2 = display_get_u8g2(&engine.display);

  uint32_t dt = 0;
  uint32_t peripheral_update_counter = 0;
  watchdog_enable(200, 1);
  while (1) {
    absolute_time_t now = time_us_64();
    uint32_t dt = absolute_time_diff_us(engine.now, now) + dt;
    divmod_result_t res = hw_divider_divmod_u32(dt, TICK_INTERVAL_US);
    uint32_t ticks = to_quotient_u32(res);
    dt = to_remainder_u32(res);
    engine.now = now;

    // update buttons
    read_button(&engine.buttons.left, now);
    read_button(&engine.buttons.right, now);
    read_button(&engine.buttons.menu, now);

    handle_menu_reset();

    ti_start(&ti_tick);
    while (ticks--) {
      if (engine.app->tick != NULL) {
        engine.app->tick();
        // reset button edge. if no tick(), they will instead be reset next
        // frame
        engine.buttons.left.edge = false;
        engine.buttons.right.edge = false;
        engine.buttons.menu.edge = false;
      }

      peripheral_update_counter++;
      if (peripheral_update_counter >= UPDATE_PERIPHERAL_EVERY) {
        peripheral_read_inputs(&engine.peripheral);
        peripheral_update_counter = 0;
      }
      engine.tick++;
    }

    // draw screen buffer
    u8g2_ClearBuffer(u8g2);

    if (engine.app->frame != NULL)
      engine.app->frame();

    // u8g2_SetDrawColor(u8g2, 1);
    // u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    // if (engine.peripheral.plugged_in) {
    //   u8g2_DrawStr(u8g2, 0, 30, "USB");
    // } else {
    //   u8g2_DrawStr(u8g2, 0, 30, "BAT");
    // }
    // char battery_str[8];
    // snprintf(battery_str, sizeof(battery_str), "%d",
    //          engine.peripheral.battery_level);
    // u8g2_DrawStr(u8g2, 0, 40, battery_str);

    // todo: remove
    // update();

    ti_stop(&ti_tick);
#ifdef DEBUG_FPS
    draw_fps(u8g2, fps);
#endif

    ti_start(&ti_show);
    // write display
    u8g2_SendBuffer(u8g2);
    // write LEDs
    leds_show(&engine.leds);
    ti_stop(&ti_show);

    // log fps and frame limit
    last_log_frames++;
    if (now - last_log_us > 1000000) {
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

    now = time_us_64(); // update timestamp for frame limit calc
    uint64_t spent_us = now - last_frame_us;
    if (spent_us < TARGET_FRAME_INTERVAL_US) {
      sleep_us(TARGET_FRAME_INTERVAL_US - spent_us);
      last_frame_us = time_us_64();
    } else {
      last_frame_us = now;
    }
  }
}

void engine_set_app(engine_app_t *app) {
  if (engine.app != NULL && engine.app->exit != NULL) {
    engine.app->exit();
  }
  if (app == NULL) {
    engine.app = &app_launcher;
    return;
  }
  engine.app = app;
  if (engine.app != NULL && engine.app->enter != NULL) {
    engine.app->enter();
  }
}
