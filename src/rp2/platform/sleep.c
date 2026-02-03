#include <pico/stdlib.h>

#include <shared/platform/sleep.h>

void platform_sleep_us(uint32_t us) { sleep_us(us); }

void platform_sleep_ms(uint32_t ms) { sleep_ms(ms); }
