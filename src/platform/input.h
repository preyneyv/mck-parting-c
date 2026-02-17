#pragma once

#include <stdint.h>

void platform_input_init(void);

typedef uint32_t platform_input_mask_t;

enum {
  PLATFORM_INPUT_LEFT = (1u << 0),
  PLATFORM_INPUT_RIGHT = (1u << 1),
  PLATFORM_INPUT_MENU = (1u << 2),
};

platform_input_mask_t platform_input_read_mask(void);
