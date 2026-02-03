#include <stddef.h>

#include <pico/unique_id.h>

#include <shared/platform/unique_id.h>

void platform_get_unique_id(platform_unique_id_t *out_id) {
  pico_unique_board_id_t board_id;
  pico_get_unique_board_id(&board_id);
  for (size_t i = 0; i < sizeof(board_id.id); i++) {
    out_id->id[i] = board_id.id[i];
  }
}
