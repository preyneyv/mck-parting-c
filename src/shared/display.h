// HAL for display OLED and 9V regulator

#pragma once

#include <u8g2.h>

typedef struct {
  u8g2_t u8g2;
  bool enabled;
} display_t;

static inline u8g2_t *display_get_u8g2(display_t *display) {
  return &display->u8g2;
}

void display_init(display_t *display);
void display_set_enabled(display_t *display, bool enabled);
