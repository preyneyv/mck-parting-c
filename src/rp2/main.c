/**
 * core 0: game logic and ui
 * core 1: audio synthesis and playback
 */

#include <math.h>
#include <stdio.h>

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/resets.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <hardware/xosc.h>
#include <pico/multicore.h>
#include <pico/stdlib.h>

#include <shared/engine.h>
#include <shared/utils/timing.h>

#include "audio.h"
#include "config.h"

void core1_main() {
  audio_playback_init();
  audio_playback_run_forever(&g_engine.synth);
}

void core0_main() { engine_run_forever(); }

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

void engine_sleep_until_interrupt() {
  uint32_t event = IO_BANK0_DORMANT_WAKE_INTE0_GPIO0_EDGE_LOW_BITS;

  gpio_set_dormant_irq_enabled(BUTTON_PIN_L, event, true);
  gpio_set_dormant_irq_enabled(BUTTON_PIN_R, event, true);
  gpio_set_dormant_irq_enabled(BUTTON_PIN_M, event, true);

  xosc_dormant();

  gpio_acknowledge_irq(BUTTON_PIN_L, event);
  gpio_acknowledge_irq(BUTTON_PIN_R, event);
  gpio_acknowledge_irq(BUTTON_PIN_M, event);
}

int main() {
  // hack to limit power draw until after usb stack is initialized
  // since macos doesn't like it when the usb device draws too much power
  // immediately.
  // todo: see if we can do this better
  gpio_init(PERIPH_BAT_CHG_EN_N);
  gpio_set_dir(PERIPH_BAT_CHG_EN_N, GPIO_OUT);
  gpio_put(PERIPH_BAT_CHG_EN_N, 1);
  sleep_ms(1000);

  // set clock for audio and LED timing reasons
  set_sys_clock_hz(SYS_CLOCK_HZ, true);
  stdio_init_all();
  sleep_ms(500);
  while (!stdio_usb_connected())
    sleep_ms(100);

  printf("hey its me prism\n");
  engine_init();

  // start audio core
  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  // start main core
  core0_main();
  return 0;
}
