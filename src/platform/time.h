#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t platform_time_t;

#define PLATFORM_TIME_ZERO ((platform_time_t)0)
#define PLATFORM_TIME_END ((platform_time_t)UINT64_MAX)

platform_time_t platform_now_us(void);
void platform_sleep_us(uint32_t us);
void platform_sleep_ms(uint32_t ms);

// Returns (to - from) in microseconds.
static inline int64_t platform_time_diff_us(platform_time_t from,
                                            platform_time_t to) {
  return (int64_t)(to - from);
}

static inline platform_time_t platform_time_add_ms(platform_time_t t,
                                                   uint32_t ms) {
  return t + ((platform_time_t)ms * 1000ULL);
}

static inline bool platform_time_reached(platform_time_t t) {
  return platform_now_us() >= t;
}
