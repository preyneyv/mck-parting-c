#include <stdlib.h>

#include <platform/system.h>

static uint32_t watchdog_stage;
static uint32_t watchdog_detail;

void platform_watchdog_enable(uint32_t timeout_ms) { (void)timeout_ms; }

void platform_watchdog_disable(void) {}

void platform_watchdog_update(void) {}

void platform_watchdog_trace(uint32_t stage, uint32_t detail)
{
  watchdog_stage = stage;
  watchdog_detail = detail;
}

uint32_t platform_watchdog_trace_stage(void) { return watchdog_stage; }
uint32_t platform_watchdog_trace_detail(void) { return watchdog_detail; }

void platform_system_reset(void) { exit(0); }
void platform_system_bootsel(void) { exit(0); }
