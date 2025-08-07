// HAL for peripheral devices (battery, LEDs)

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "config.h"

typedef struct {
  bool enabled;    // true if peripheral power is enabled (LEDs, battery ADC)
  bool plugged_in; // true if VBUS is present (USB power)
  bool charging_enabled; // true if battery charging is enabled
  bool charging;         // true if battery is charging
  uint8_t battery_level; // 0-100% battery level
} peripheral_t;

void peripheral_init(peripheral_t *p);
void peripheral_set_enabled(peripheral_t *p, bool enabled);
void peripheral_set_charging_enabled(peripheral_t *p, bool enabled);
void peripheral_read_inputs(peripheral_t *p);
