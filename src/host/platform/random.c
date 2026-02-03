#include <stdbool.h>
#include <stdlib.h>
#include <time.h>

#include <shared/platform/random.h>

uint32_t platform_random_u32() {
  static bool seeded = false;
  if (!seeded) {
    seeded = true;
    srand((unsigned)time(NULL));
  }
  uint32_t hi = (uint32_t)(rand() & 0xFFFF);
  uint32_t lo = (uint32_t)(rand() & 0xFFFF);
  return (hi << 16) | lo;
}
