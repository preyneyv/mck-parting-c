#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include <prism/asset_pack.h>
#include <prism/cartridge_identity.h>
#include <prism/management_serialization.h>
#include <shared/os/asset_pack.h>
#include <cartridges/beatline/format.h>

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

static void test_pack_identity_and_paths(void)
{
  static const uint8_t expected[PRISM_PACK_KEY_BYTES] = {
      0xe3, 0x9c, 0x94, 0x03, 0x05, 0xc5, 0x41, 0xc3,
      0xe1, 0xa1, 0xb6, 0x7b, 0xd1, 0x94, 0x6f, 0xb1,
  };
  prism_pack_key_t key;
  assert(prism_pack_key_derive(
      "dev.preyneyv.prism.beatline.golden", key));
  assert(memcmp(key, expected, sizeof(key)) == 0);

  assert(prism_asset_path_valid_n("tracks/golden.beatline", 22));
  const char *invalid[] = {"", "/track", "track/", "track//map",
                           "track/../map", "track\\map"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    assert(!prism_asset_path_valid_n(invalid[i], strlen(invalid[i])));
}

static uint32_t append_string(uint8_t *package, uint32_t *cursor,
                              const char *text)
{
  uint32_t offset = *cursor;
  size_t length = strlen(text) + 1u;
  memcpy(package + offset, text, length);
  *cursor += (uint32_t)length;
  return offset;
}

static void test_pack_parsing(void)
{
  uint8_t package[512] = {0};
  prism_asset_pack_header_t *header = (void *)package;
  prism_asset_pack_file_entry_t *entry =
      (void *)(package + PRISM_ASSET_PACK_HEADER_BYTES);
  *header = (prism_asset_pack_header_t){
      .magic = PRISM_ASSET_PACK_MAGIC,
      .format_version = PRISM_ASSET_PACK_FORMAT_VERSION,
      .header_size = sizeof(*header),
      .version = 2,
      .target_min_version = 1,
      .target_max_version = 3,
      .file_count = 1,
      .directory_offset = PRISM_ASSET_PACK_HEADER_BYTES,
      .string_table_offset = PRISM_ASSET_PACK_HEADER_BYTES + sizeof(*entry),
  };
  uint32_t cursor = header->string_table_offset;
  header->id_offset = append_string(
      package, &cursor, "dev.preyneyv.prism.beatline.golden");
  header->id_length = 34;
  header->name_offset = append_string(package, &cursor, "Golden");
  header->name_length = 6;
  header->target_id_offset = append_string(
      package, &cursor, "dev.preyneyv.prism.beatline");
  header->target_id_length = 27;
  entry->path_offset = append_string(package, &cursor,
                                     "tracks/golden.beatline");
  entry->path_length = 22;
  header->string_table_size = cursor - header->string_table_offset;
  header->data_offset = (cursor + 3u) & ~3u;
  entry->data_offset = header->data_offset;
  entry->data_length = 4;
  memcpy(package + entry->data_offset, "song", 4);
  header->package_size = entry->data_offset + entry->data_length;
  assert(prism_pack_key_derive((const char *)package + header->id_offset,
                               header->pack_key));
  assert(prism_app_key_derive(
      (const char *)package + header->target_id_offset,
      header->target_app_key));

  prism_asset_pack_view_t view;
  assert(prism_asset_pack_parse(package, sizeof(package), &view));
  assert(strcmp(view.id, "dev.preyneyv.prism.beatline.golden") == 0);
  assert(strcmp(view.target_id, "dev.preyneyv.prism.beatline") == 0);
  prism_asset_file_t file;
  assert(prism_asset_pack_file_at(&view, 0, &file));
  assert(strcmp(file.path, "tracks/golden.beatline") == 0);
  assert(file.size == 4 && memcmp(file.data, "song", 4) == 0);

  header->pack_key[0] ^= 1u;
  assert(!prism_asset_pack_parse(package, sizeof(package), &view));
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

static void test_asset_pack_management_serialization(void)
{
  uint8_t payload[512];
  prism_management_asset_pack_list_init(payload, sizeof(payload), 1, 0);
  prism_management_asset_pack_entry_t entry = {
      .package_bytes = 34567,
      .version = 2,
      .blocks = 1,
      .status = PRISM_ASSET_PACK_STATUS_INCOMPATIBLE,
  };
  memset(entry.pack_key, 0x33, sizeof(entry.pack_key));
  memset(entry.target_app_key, 0x44, sizeof(entry.target_app_key));
  assert(prism_management_asset_pack_list_append(
      payload, sizeof(payload), &entry,
      "dev.preyneyv.prism.beatline.golden", "Golden",
      "dev.preyneyv.prism.beatline"));

  const prism_management_cartridge_list_t *list = (const void *)payload;
  assert(list->count == 1 && list->string_bytes == 67);
  const prism_management_asset_pack_entry_t *serialized =
      (const void *)(payload + sizeof(*list));
  const char *strings = (const char *)(serialized + 1);
  assert(serialized->package_bytes == 34567 && serialized->version == 2);
  assert(serialized->id_length == 34 && serialized->name_length == 6 &&
         serialized->target_id_length == 27);
  assert(memcmp(strings + serialized->target_id_offset,
                "dev.preyneyv.prism.beatline", 27) == 0);
  assert(prism_management_asset_pack_list_size(payload) ==
         sizeof(*list) + sizeof(*serialized) + list->string_bytes);
}

static void test_beatline_leaderboard_payload(void)
{
  static const uint8_t expected[BEATLINE_LEADERBOARD_PAYLOAD_BYTES] = {
      0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01,
      0x78, 0x56, 0x34, 0x12, 0x02, 0x01, 0x04, 0x03,
      0x06, 0x05, 0x08, 0x07, 0x0a, 0x09,
  };
  uint8_t payload[BEATLINE_LEADERBOARD_PAYLOAD_BYTES];
  beatline_leaderboard_payload(payload, UINT64_C(0x0123456789abcdef),
                               UINT32_C(0x12345678), 0x0102, 0x0304,
                               0x0506, 0x0708, 0x090a);
  assert(sizeof(payload) == 22);
  assert(memcmp(payload, expected, sizeof(payload)) == 0);
}

int main(void)
{
  test_ids();
  test_key();
  test_pack_identity_and_paths();
  test_pack_parsing();
  test_updates();
  test_management_serialization();
  test_asset_pack_management_serialization();
  test_beatline_leaderboard_payload();
  return 0;
}
