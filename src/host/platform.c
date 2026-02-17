#include <platform/display.h>
#include <platform/input.h>
#include <platform/leds.h>
#include <platform/peripheral.h>
#include <platform/platform.h>

void platform_init(void) {
  platform_input_init();
  platform_peripheral_init();
  platform_display_init();
  platform_leds_init();
}

void platform_task(void) {
  platform_peripheral_read_inputs();
}
