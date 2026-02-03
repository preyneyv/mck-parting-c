#include <shared/platform/watchdog.h>

void platform_watchdog_enable(uint32_t delay_ms, bool pause_on_debug) {
  (void)delay_ms;
  (void)pause_on_debug;
}

void platform_watchdog_disable() {}

void platform_watchdog_update() {}
