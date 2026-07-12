#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <prism/cartridge_identity.h>
#include <prism/management_serialization.h>

static void test_ids(void)
{
  assert(prism_cartridge_id_valid("dev.preyneyv.prism.beatline"));
  const char *invalid[] = {"", ".dev.app", "dev.app.", "dev..app",
                           "Dev.app", "dev_app", "-dev.app", "dev-.app"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    assert(!prism_cartridge_id_valid(invalid[i]));
  char label[67];
  memset(label, 'a', 64);
  memcpy(label + 64, ".b", 3);
  assert(!prism_cartridge_id_valid(label));
  char maximum[254];
  size_t offset = 0;
  const size_t lengths[] = {63, 63, 63, 61};
  for (size_t i = 0; i < 4; ++i)
  {
    memset(maximum + offset, 'a', lengths[i]);
    offset += lengths[i];
    if (i != 3)
      maximum[offset++] = '.';
  }
  maximum[offset] = '\0';
  assert(offset == 253 && prism_cartridge_id_valid(maximum));
}

static void test_key(void)
{
  static const uint8_t expected[PRISM_APP_KEY_BYTES] = {
      0x32, 0xb4, 0x1a, 0x03, 0xab, 0x53, 0x33, 0x63,
      0x15, 0xfc, 0x9e, 0x3f, 0x33, 0xe1, 0x7e, 0x40,
  };
  prism_app_key_t first;
  prism_app_key_t second;
  assert(prism_app_key_derive("dev.preyneyv.prism.beatline", first));
  assert(prism_app_key_derive("dev.preyneyv.prism.beatline", second));
  assert(memcmp(first, expected, sizeof(first)) == 0);
  assert(memcmp(first, second, sizeof(first)) == 0);
}

static void test_updates(void)
{
  const char *id = "dev.preyneyv.prism.beatline";
  prism_app_key_t key;
  prism_app_key_t other_key;
  assert(prism_app_key_derive(id, key));
  assert(prism_app_key_derive("dev.example.other", other_key));
  assert(prism_cartridge_update_check(key, id, 3, key, id, 3) ==
         PRISM_CARTRIDGE_UPDATE_MATCH);
  assert(prism_cartridge_update_check(key, id, 3, key, id, 4) ==
         PRISM_CARTRIDGE_UPDATE_MATCH);
  assert(prism_cartridge_update_check(key, id, 3, key, id, 2) ==
         PRISM_CARTRIDGE_UPDATE_DOWNGRADE);
  assert(prism_cartridge_update_check(key, id, 3, key,
                                      "dev.example.other", 4) ==
         PRISM_CARTRIDGE_UPDATE_KEY_COLLISION);
  assert(prism_cartridge_update_check(key, id, 3, other_key,
                                      "dev.example.other", 1) ==
         PRISM_CARTRIDGE_UPDATE_SEPARATE);
}

static void test_management_serialization(void)
{
  uint8_t payload[512];
  prism_management_cartridge_list_init(payload, sizeof(payload), 2, 0);
  prism_management_cartridge_entry_t first = {
      .package_bytes = 1234,
      .persistent_bytes = 64,
      .version = 3,
      .blocks = 1,
  };
  memset(first.app_key, 0x11, sizeof(first.app_key));
  assert(prism_management_cartridge_list_append(
      payload, sizeof(payload), &first, "dev.preyneyv.prism.beatline",
      "old name"));
  prism_management_cartridge_entry_t second = {.version = 4, .blocks = 2};
  memset(second.app_key, 0x22, sizeof(second.app_key));
  assert(prism_management_cartridge_list_append(
      payload, sizeof(payload), &second, "dev.preyneyv.prism.beatline-beta",
      "new name"));

  const prism_management_cartridge_list_t *list = (const void *)payload;
  assert(list->count == 2 && list->string_bytes == 75);
  const prism_management_cartridge_entry_t *entries =
      (const void *)(payload + sizeof(*list));
  const char *strings =
      (const char *)(entries + list->count);
  assert(entries[0].version == 3 && entries[1].version == 4);
  assert(entries[0].id_length == 27 && entries[0].name_length == 8);
  assert(memcmp(strings + entries[0].id_offset,
                "dev.preyneyv.prism.beatline", entries[0].id_length) == 0);
  assert(memcmp(strings + entries[1].name_offset, "new name",
                entries[1].name_length) == 0);
  assert(prism_management_cartridge_list_size(payload) ==
         sizeof(*list) + 2 * sizeof(*entries) + list->string_bytes);
}

int main(void)
{
  test_ids();
  test_key();
  test_updates();
  test_management_serialization();
  return 0;
}
