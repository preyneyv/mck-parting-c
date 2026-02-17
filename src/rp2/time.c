#include <pico/stdlib.h>

#include <platform/time.h>

platform_time_t platform_now_us(void) { return to_us_since_boot(get_absolute_time()); }

void platform_sleep_us(uint32_t us) { sleep_us(us); }

void platform_sleep_ms(uint32_t ms) { sleep_ms(ms); }
