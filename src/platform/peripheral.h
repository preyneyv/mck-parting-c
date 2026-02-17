#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool plugged_in;
  bool charging;
  uint8_t battery_level;
} platform_power_state_t;

void platform_peripheral_init(void);
void platform_peripheral_set_enabled(bool enabled);
void platform_peripheral_set_charging_enabled(bool enabled);
void platform_peripheral_read_inputs(void);
platform_power_state_t platform_peripheral_get_power_state(void);

bool platform_peripheral_is_enabled(void);
bool platform_peripheral_is_plugged_in(void);
bool platform_peripheral_is_charging_enabled(void);
bool platform_peripheral_is_charging(void);
uint8_t platform_peripheral_battery_level(void);
