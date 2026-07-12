#pragma once

#include <stddef.h>
#include <stdint.h>

#include <prism/graphics/led.h>

#define LED_COUNT PRISM_LED_COUNT
#define LED_L PRISM_LED_LEFT
#define LED_R PRISM_LED_RIGHT

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "This code assumes a little-endian system!"
#endif

void platform_leds_init(void);
void platform_leds_set_brightness(uint8_t brightness);
void platform_leds_show(const color_t *colors, size_t count);
