#include <string.h>

#include <shared/platform/random.h>
#include <shared/platform/unique_id.h>

void platform_get_unique_id(platform_unique_id_t *out_id) {
  for (size_t i = 0; i < sizeof(out_id->id); i += sizeof(uint32_t)) {
    uint32_t value = platform_random_u32();
    size_t remaining = sizeof(out_id->id) - i;
    size_t copy = remaining < sizeof(uint32_t) ? remaining : sizeof(uint32_t);
    memcpy(out_id->id + i, &value, copy);
  }
}
