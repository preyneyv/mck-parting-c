#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include <shared/platform/identity.h>
#include <shared/platform/system.h>
#include <shared/platform/time.h>

#ifdef _WIN32
#include <windows.h>
#endif

static int g_seeded = 0;
static uint8_t g_device_id[8] = {0x48, 0x4f, 0x53, 0x54, 0, 0, 0, 1};

platform_time_t platform_now_us(void) {
  struct timespec ts;
  timespec_get(&ts, TIME_UTC);
  return ((platform_time_t)ts.tv_sec * 1000000ULL) +
         ((platform_time_t)ts.tv_nsec / 1000ULL);
}

void platform_sleep_us(uint32_t us) {
#ifdef _WIN32
  DWORD ms = (DWORD)((us + 999u) / 1000u);
  Sleep(ms);
#else
  struct timespec ts;
  ts.tv_sec = us / 1000000u;
  ts.tv_nsec = (long)(us % 1000000u) * 1000L;
  nanosleep(&ts, NULL);
#endif
}

void platform_sleep_ms(uint32_t ms) { platform_sleep_us(ms * 1000u); }

void platform_watchdog_enable(uint32_t timeout_ms) { (void)timeout_ms; }

void platform_watchdog_disable(void) {}

void platform_watchdog_update(void) {}

void platform_system_reset(void) { exit(0); }

void platform_device_id(uint8_t out[8]) {
  for (int i = 0; i < 8; i++) {
    out[i] = g_device_id[i];
  }
}

uint32_t platform_rand_u32(void) {
  if (!g_seeded) {
    srand((unsigned int)platform_now_us());
    g_seeded = 1;
  }
  return ((uint32_t)rand() << 16) ^ (uint32_t)rand();
}
