#include <stdint.h>
#include <stdlib.h>

#include <platform/identity.h>
#include <platform/time.h>

static int g_seeded = 0;
static uint8_t g_device_id[8] = {0x48, 0x4f, 0x53, 0x54, 0, 0, 0, 1};

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
