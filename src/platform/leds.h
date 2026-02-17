#pragma once

#include <stddef.h>
#include <stdint.h>

#define LED_COUNT 2
#define LED_L 0
#define LED_R 1

#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "This code assumes a little-endian system!"
#endif

typedef union {
  struct __attribute__((packed)) {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
  };
  uint32_t hex;
} color_t;

void platform_leds_init(void);
void platform_leds_show(const color_t *colors, size_t count);

static inline color_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return (color_t){.r = r, .g = g, .b = b, .a = a};
}
