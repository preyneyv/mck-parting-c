
#include <hardware/pio.h>

#include <shared/leds.h>

#include "config.h"
#include "leds.pio.h"

static const uint8_t CYCLES_PER_BIT =
    led_write_T1 + led_write_T2 + led_write_T3; // measured from led.pio
static const uint32_t SM_BCLK = LED_BITRATE_HZ * CYCLES_PER_BIT;
static const uint32_t SM_CLKDIV_INT = SYS_CLOCK_HZ / SM_BCLK;
static const uint32_t SM_CLKDIV_FRAC = (SYS_CLOCK_HZ % SM_BCLK) * 256 / SM_BCLK;

static uint8_t led_sm;

void leds_init(leds_t *leds) {
  PIO pio = LED_PIO;
  led_sm = pio_claim_unused_sm(pio, true);

  int offset = pio_add_program(pio, &led_write_program);
  if (offset < 0) {
    panic("Failed to add LED program to PIO");
  }

  pio_gpio_init(pio, LED_DOUT);
  pio_sm_set_consecutive_pindirs(pio, led_sm, LED_DOUT, 1, true);

  pio_sm_config c = led_write_program_get_default_config(offset);
  sm_config_set_sideset_pins(&c, LED_DOUT);
  sm_config_set_out_shift(&c, false, true, 24);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv_int_frac8(&c, SM_CLKDIV_INT, SM_CLKDIV_FRAC);

  pio_sm_init(pio, led_sm, offset, &c);
  pio_sm_set_enabled(pio, led_sm, true);
}

static uint32_t color_to_led_bytes(color_t color) {
  // apply color correction and convert to GRB0 order
  // tailing zero because PIO expects 32-bit words
  return (color.g * ((LED_CORRECTION >> 8) & 0xff) >> 8) << 24 |
         (color.r * ((LED_CORRECTION >> 16) & 0xff) >> 8) << 16 |
         (color.b * (LED_CORRECTION & 0xff) >> 8) << 8;
}

void leds_show(leds_t *leds) {
  for (int i = 0; i < LED_COUNT; i++) {
    pio_sm_put_blocking(LED_PIO, led_sm, color_to_led_bytes(leds->colors[i]));
  }
}

void leds_set_all(leds_t *leds, color_t color) {
  for (int i = 0; i < LED_COUNT; i++) {
    leds->colors[i] = color;
  }
}
