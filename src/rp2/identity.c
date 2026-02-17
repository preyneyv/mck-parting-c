#include <pico/rand.h>
#include <pico/unique_id.h>

#include <platform/identity.h>

void platform_device_id(uint8_t out[8]) {
  pico_unique_board_id_t board_id;
  pico_get_unique_board_id(&board_id);
  for (int i = 0; i < 8; i++) {
    out[i] = board_id.id[i];
  }
}

uint32_t platform_rand_u32(void) { return get_rand_32(); }
