#include <pico/rand.h>

#include <shared/platform/random.h>

uint32_t platform_random_u32() { return get_rand_32(); }
