#include <unistd.h>

#include <shared/platform/sleep.h>

void platform_sleep_us(uint32_t us) { usleep(us); }

void platform_sleep_ms(uint32_t ms) { usleep(ms * 1000); }
