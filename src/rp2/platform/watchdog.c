#include <hardware/watchdog.h>

#include <shared/platform/watchdog.h>

void platform_watchdog_enable(uint32_t delay_ms, bool pause_on_debug) {
  watchdog_enable(delay_ms, pause_on_debug);
}

void platform_watchdog_disable() { watchdog_disable(); }

void platform_watchdog_update() { watchdog_update(); }
