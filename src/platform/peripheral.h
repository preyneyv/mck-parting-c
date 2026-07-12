#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <prism/types.h>

typedef prism_power_state_t platform_power_state_t;

void platform_peripheral_init(void);
void platform_peripheral_set_enabled(bool enabled);
void platform_peripheral_set_charging_enabled(bool enabled);
void platform_peripheral_read_inputs(void);
platform_power_state_t platform_peripheral_get_power_state(void);
