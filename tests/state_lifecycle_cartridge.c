#include <stddef.h>
#include <stdint.h>

#include <prism/sdk.h>

#include "state_lifecycle_protocol.h"

static const uint8_t icon[PRISM_CARTRIDGE_ICON_BYTES] = {0};
static uint32_t zero_value;
static uint32_t initialized_value = 0x12345678u;
static uint32_t numbers[] = {11u, 22u};
static uint8_t large_bss[8192];
static uint32_t *pointer_table[] = {&numbers[1], &zero_value};
static const char *writable_strings[] = {"reset"};

static void enter(prism_t *prism)
{
  prism_state_lifecycle_report_t *report = prism->persistent;
  if (report == NULL ||
      prism->persistent_size < sizeof(prism_state_lifecycle_report_t))
    return;

  *report = (prism_state_lifecycle_report_t){
      .magic = PRISM_STATE_LIFECYCLE_REPORT_MAGIC,
      .zero_value = zero_value,
      .initialized_value = initialized_value,
      .first_number = numbers[0],
      .second_number = numbers[1],
      .pointer_to_number_value = *pointer_table[0],
      .pointer_to_zero_matches = pointer_table[1] == &zero_value,
      .large_bss_first = large_bss[0],
      .large_bss_last = large_bss[sizeof(large_bss) - 1],
      .writable_string_first = (uint8_t)writable_strings[0][0],
  };

  zero_value = 99u;
  initialized_value = 0u;
  numbers[0] = 33u;
  numbers[1] = 44u;
  pointer_table[0] = &numbers[0];
  pointer_table[1] = &numbers[1];
  writable_strings[0] = "mutated";
  large_bss[0] = 0xa5u;
  large_bss[sizeof(large_bss) - 1] = 0x5au;
}

static void frame(prism_t *prism)
{
  u8g2_DrawPixel(prism_display(prism), 0, 0);
}

PRISM_CARTRIDGE(cartridge_state_lifecycle,
    .id = "dev.preyneyv.prism.state-lifecycle-test",
    .name = "state lifecycle test",
    .version = 1,
    .icon = icon,
    .enter = enter,
    .frame = frame,
    .persistent_size = sizeof(prism_state_lifecycle_report_t),
    .persistent_schema_version = 1,
);
