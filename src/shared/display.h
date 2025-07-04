#pragma once

#include <u8g2.h>

typedef struct {
  u8g2_t u8g2;
} display_t;

inline u8g2_t *display_get_u8g2(display_t *display) { return &display->u8g2; }
