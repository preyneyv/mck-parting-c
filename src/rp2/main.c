/**
 * core 0: game logic and ui
 * core 1: audio synthesis and playback
 */

#include <math.h>
#include <stdio.h>

#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>

#include <shared/engine.h>
#include <shared/utils/timing.h>

#include "audio.h"
#include "config.h"

void core1_main() {
  // todo: event handling, timeline controller
  audio_loop(&engine.audio);
}

void core0_main() {
  // setup buttons
  engine_run_forever();
}

void engine_buttons_init() {
  gpio_init(BUTTON_PIN_L);
  gpio_set_dir(BUTTON_PIN_L, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_L);

  gpio_init(BUTTON_PIN_R);
  gpio_set_dir(BUTTON_PIN_R, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_R);

  gpio_init(BUTTON_PIN_M);
  gpio_set_dir(BUTTON_PIN_M, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_M);
}

bool engine_button_read(button_id_t button_id) {
  uint gpio;
  switch (button_id) {
  case BUTTON_LEFT:
    gpio = BUTTON_PIN_L;
    break;
  case BUTTON_RIGHT:
    gpio = BUTTON_PIN_R;
    break;
  case BUTTON_MENU:
    gpio = BUTTON_PIN_M;
    break;
  }
  return 1 - gpio_get(gpio); // active low
}

int main() {
  // set clock for audio PWM reasons
  set_sys_clock_hz(SYS_CLOCK_HZ, true);
  stdio_init_all();

  while (!stdio_usb_connected()) {
    sleep_ms(10);
  }

  engine_init();

  // kick off audio core
  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  // kick off main core
  core0_main();
  return 0;
}
