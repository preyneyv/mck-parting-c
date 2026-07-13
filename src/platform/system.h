#pragma once

#include <stdint.h>

void platform_watchdog_enable(uint32_t timeout_ms);
void platform_watchdog_disable(void);
void platform_watchdog_update(void);
void platform_watchdog_trace(uint32_t stage, uint32_t detail);
uint32_t platform_watchdog_trace_stage(void);
uint32_t platform_watchdog_trace_detail(void);
void platform_system_reset(void);
void platform_system_bootsel(void);
