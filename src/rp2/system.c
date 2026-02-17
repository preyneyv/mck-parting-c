#include <hardware/watchdog.h>

#include <platform/system.h>

void platform_watchdog_enable(uint32_t timeout_ms) { watchdog_enable(timeout_ms, 1); }

void platform_watchdog_disable(void) { watchdog_disable(); }

void platform_watchdog_update(void) { watchdog_update(); }

void platform_system_reset(void) {
  watchdog_enable(0, 0);
  while (1) {
  }
}
