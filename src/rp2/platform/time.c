#include <limits.h>

#include <pico/time.h>

#include <shared/platform/time.h>

platform_time_t platform_time_now() {
  return (platform_time_t)to_us_since_boot(get_absolute_time());
}

int64_t platform_time_diff_us(platform_time_t start, platform_time_t end) {
  return end - start;
}

platform_time_t platform_time_delayed_by_ms(platform_time_t start, int32_t ms) {
  return start + ((platform_time_t)ms * 1000);
}

bool platform_time_reached(platform_time_t target) {
  return platform_time_now() >= target;
}

platform_time_t platform_time_nil() { return 0; }

platform_time_t platform_time_at_end() { return INT64_MAX; }
