#pragma once

#include <stdint.h>

void platform_watchdog_enable(uint32_t timeout_ms);
void platform_watchdog_disable(void);
void platform_watchdog_update(void);
void platform_system_reset(void);
