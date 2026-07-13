#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>
#include <pico/bootrom.h>

#include <platform/system.h>

void platform_watchdog_enable(uint32_t timeout_ms) { watchdog_enable(timeout_ms, 1); }

void platform_watchdog_disable(void) { watchdog_disable(); }

void platform_watchdog_update(void) { watchdog_update(); }

void platform_watchdog_trace(uint32_t stage, uint32_t detail)
{
  watchdog_hw->scratch[0] = stage;
  watchdog_hw->scratch[1] = detail;
}

uint32_t platform_watchdog_trace_stage(void) { return watchdog_hw->scratch[0]; }
uint32_t platform_watchdog_trace_detail(void) { return watchdog_hw->scratch[1]; }

void platform_system_reset(void) {
  watchdog_enable(0, 0);
  while (1) {
  }
}

void platform_system_bootsel(void) { reset_usb_boot(0, 0); }
