#pragma once

#include <stdbool.h>
#include <stdint.h>

void platform_watchdog_enable(uint32_t delay_ms, bool pause_on_debug);
void platform_watchdog_disable();
void platform_watchdog_update();
