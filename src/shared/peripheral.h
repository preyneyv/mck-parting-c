// HAL for peripheral devices (battery, LEDs)

#pragma once

#include "config.h"

typedef struct {
  bool enabled;
} peripheral_t;

void peripheral_init(peripheral_t *p);
void peripheral_set_enabled(peripheral_t *p, bool enabled);
