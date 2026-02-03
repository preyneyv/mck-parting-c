#pragma once

#include <stdint.h>

typedef struct {
  uint8_t id[8];
} platform_unique_id_t;

void platform_get_unique_id(platform_unique_id_t *out_id);
