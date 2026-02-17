#include <platform/leds.h>

void platform_leds_init(void) {}

void platform_leds_show(const color_t *colors, size_t count) {
  (void)colors;
  (void)count;
}
