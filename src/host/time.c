#include <stdint.h>
#include <time.h>

#include <platform/time.h>

#ifdef _WIN32
#include <windows.h>
#endif

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
