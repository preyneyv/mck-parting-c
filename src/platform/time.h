#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <prism/types.h>

typedef prism_time_t platform_time_t;

#define PLATFORM_TIME_ZERO PRISM_TIME_ZERO
#define PLATFORM_TIME_END PRISM_TIME_END

platform_time_t platform_now_us(void);
void platform_sleep_us(uint32_t us);
void platform_sleep_ms(uint32_t ms);

static inline int64_t platform_time_diff_us(platform_time_t from,
                                            platform_time_t to) {
  return to >= from ? (int64_t)(to - from) : -(int64_t)(from - to);
}

static inline platform_time_t platform_time_add_ms(platform_time_t t,
                                                   uint32_t ms) {
  return t + ((platform_time_t)ms * 1000ULL);
}

static inline bool platform_time_reached(platform_time_t t) {
  return platform_now_us() >= t;
}
