#include <stdio.h>

#include <shared/utils/timing.h>

#include "engine.h"

#define DEBUG_FPS

// global engine instance
engine_t engine;

void engine_init() {
  // initialize all subsystems
  audio_init(&engine.audio);
  display_init(&engine.display);
  peripheral_init(&engine.peripheral);
  leds_init(&engine.leds);

  engine.buttons.left.id = BUTTON_LEFT;
  engine.buttons.right.id = BUTTON_RIGHT;
  engine.buttons.menu.id = BUTTON_MENU;
  engine_buttons_init(&engine.buttons);
}

static void draw_fps(u8g2_t *u8g2, uint32_t fps) {
  static int fps_state = 0;
  u8g2_SetDrawColor(u8g2, 0);
  u8g2_DrawBox(u8g2, 0, 0, 16, 4);
  u8g2_SetDrawColor(u8g2, 1);
  fps_state = (fps_state + 1) % 10;
  if (fps_state < 4) {
    u8g2_DrawPixel(u8g2, 1, fps_state);
  } else {
    u8g2_DrawPixel(u8g2, 0, 7 - fps_state);
  }
  u8g2_SetFont(u8g2, u8g2_font_u8glib_4_tr);
  char fps_str[8];
  snprintf(fps_str, sizeof(fps_str), "%d", fps);
  u8g2_DrawStr(u8g2, 3, 4, fps_str);
}

static void read_button(button_t *button, absolute_time_t now) {
  bool pressed = engine_button_read(button->id);
  if (pressed) {
    if (button->pressed) {
      // held down
      button->transitioned = false;
    } else {
      button->pressed = true;
      button->pressed_at = now;
      button->transitioned = true;
    }
  } else {
    if (button->pressed) {
      // released
      button->pressed = false;
      button->pressed_at = nil_time;
      button->transitioned = true;
    } else {
      // held up
      button->transitioned = false;
    }
  }
}

void engine_run_forever() {
  display_set_enabled(&engine.display, true);
  peripheral_set_enabled(&engine.peripheral, true);

  TimingInstrumenter ti_tick;
  TimingInstrumenter ti_show;

  uint64_t last_frame_us = 0;
  uint64_t last_log_us = 0;
  uint32_t last_log_frames = 0;
  uint32_t fps = 0;

  u8g2_t *u8g2 = display_get_u8g2(&engine.display);
  while (1) {
    absolute_time_t now = time_us_64();
    engine.now = now;
    // todo: delta time, ticks

    ti_start(&ti_tick);

    // update buttons
    read_button(&engine.buttons.left, now);
    read_button(&engine.buttons.right, now);
    read_button(&engine.buttons.menu, now);

    // draw screen buffer
    u8g2_ClearBuffer(u8g2);

    u8g2_SetDrawColor(u8g2, 1);
    if (engine.buttons.left.pressed) {
      engine.leds.colors[0].r = 0xff;
      u8g2_DrawStr(u8g2, 0, 20, "Left");
    } else {
      engine.leds.colors[0].r = 0x00;
    }
    if (engine.buttons.left.transitioned) {
      u8g2_DrawStr(u8g2, 60, 20, "T");
    }
    if (engine.buttons.right.pressed) {
      engine.leds.colors[1].r = 0xff;
      u8g2_DrawStr(u8g2, 0, 30, "Right");
    } else {
      engine.leds.colors[1].r = 0x00;
    }
    if (engine.buttons.right.transitioned) {
      u8g2_DrawStr(u8g2, 60, 30, "T");
    }
    if (engine.buttons.menu.pressed) {
      u8g2_DrawStr(u8g2, 0, 40, "Menu");
    }
    if (engine.buttons.menu.transitioned) {
      u8g2_DrawStr(u8g2, 60, 40, "T");
    }

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
