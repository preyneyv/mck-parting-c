#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <pico/rand.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <tusb.h>

#include <platform/display.h>
#include <platform/input.h>
#include <platform/leds.h>
#include <platform/peripheral.h>
#include <platform/platform.h>

#include "config.h"

void platform_init(void) {
  // limit power draw until usb stack is initialized.
  gpio_init(PERIPH_BAT_CHG_EN_N);
  gpio_set_dir(PERIPH_BAT_CHG_EN_N, GPIO_OUT);
  gpio_put(PERIPH_BAT_CHG_EN_N, 1);

  set_sys_clock_hz(SYS_CLOCK_HZ, true);
  stdio_init_all();
  sleep_ms(1000);

  // initialize rng
  get_rand_32();

  platform_input_init();
  platform_peripheral_init();
  platform_display_init();
  platform_leds_init();
}

void platform_task(void) {
  tud_task();
  platform_peripheral_read_inputs();
}
