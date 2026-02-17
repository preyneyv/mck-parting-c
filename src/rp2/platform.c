#include <hardware/watchdog.h>
#include <pico/rand.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>

#include <shared/platform/identity.h>
#include <shared/platform/system.h>
#include <shared/platform/time.h>

platform_time_t platform_now_us(void) { return to_us_since_boot(get_absolute_time()); }

void platform_sleep_us(uint32_t us) { sleep_us(us); }

void platform_sleep_ms(uint32_t ms) { sleep_ms(ms); }

void platform_watchdog_enable(uint32_t timeout_ms) { watchdog_enable(timeout_ms, 1); }

void platform_watchdog_disable(void) { watchdog_disable(); }

void platform_watchdog_update(void) { watchdog_update(); }

void platform_system_reset(void) {
  watchdog_enable(0, 0);
  while (1) {
  }
}

void platform_device_id(uint8_t out[8]) {
  pico_unique_board_id_t board_id;
  pico_get_unique_board_id(&board_id);
  for (int i = 0; i < 8; i++) {
    out[i] = board_id.id[i];
  }
}

uint32_t platform_rand_u32(void) { return get_rand_32(); }
