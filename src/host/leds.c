#include <shared/leds.h>

void leds_init(leds_t *leds) {
  for (int i = 0; i < LED_COUNT; i++) {
    leds->colors[i].hex = 0;
  }
}

void leds_show(leds_t *leds) {
  (void)leds;
}

void leds_set_all(leds_t *leds, color_t color) {
  for (int i = 0; i < LED_COUNT; i++) {
    leds->colors[i] = color;
  }
}
