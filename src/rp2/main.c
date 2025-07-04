/**
 * core 0: game logic and ui
 * core 1: audio synthesis and playback
 */

#include <math.h>
#include <stdio.h>

#include <pico/multicore.h>
#include <pico/stdlib.h>

#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/spi.h>
#include <hardware/sync.h>

#include <u8g2.h>

#include <shared/utils/timing.h>

#include "audio.h"
#include "config.h"
#include "display.h"

display_t display;

void core0_main() {
  display_init(&display);

  TimingInstrumenter ti_tick;
  TimingInstrumenter ti_show;

  uint64_t last_frame_us = 0;
  uint64_t last_log_us = 0;
  uint32_t last_log_frames = 0;
  uint32_t fps = 0;

  uint32_t i = 0;

  u8g2_t *u8g2 = display_get_u8g2(&display);
  while (1) {
    ti_start(&ti_tick);
    i = (i + 1) % DISP_PIX;
    uint8_t x = i % DISP_WIDTH;
    uint8_t y = i / DISP_WIDTH;

    u8g2_FirstPage(u8g2);
    u8g2_ClearBuffer(u8g2);
    u8g2_SetDrawColor(u8g2, 0);
    u8g2_DrawBox(u8g2, 0, 0, 128, 64);

    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawPixel(u8g2, x, y); // pixel that scans L to R, T to B
    ti_stop(&ti_tick);

    ti_start(&ti_show);
    u8g2_SendBuffer(u8g2);
    ti_stop(&ti_show);

    last_log_frames++;
    uint64_t now = time_us_64();
    if (now - last_log_us > 1000000) {
      fps = last_log_frames;
      float ti_tick_avg = ti_get_average_ms(&ti_tick, true);
      float ti_show_avg = ti_get_average_ms(&ti_show, true);
      float ti_frame_avg = ti_tick_avg + ti_show_avg;
      printf("fps: %d | frame: %.2f ms | tick: %.2f ms | show: %.2f ms\n", fps,
             ti_frame_avg, ti_tick_avg, ti_show_avg);
      last_log_us = now;
      last_log_frames = 0;
    }

    uint64_t spent_us = now - last_frame_us;
    if (spent_us < TARGET_FRAME_INTERVAL_US) {
      sleep_us(TARGET_FRAME_INTERVAL_US - spent_us);
      last_frame_us = time_us_64();
    } else {
      last_frame_us = now;
    }
  }
}

void core1_main() {
  // todo: audio, synth init
  while (1) {
    __wfi();
  }
}

int main() {
  stdio_init_all();
  // set clock for audio PWM reasons
  set_sys_clock_khz(SYS_CLOCK_KHZ, true);

  // kick off audio core
  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  // kick off main core
  core0_main();
}
