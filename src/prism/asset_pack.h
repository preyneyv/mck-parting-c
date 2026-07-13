#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/cartridge_identity.h>

#define PRISM_ASSET_PACK_MAGIC 0x4b415050u /* PPAK */
#define PRISM_ASSET_PACK_FORMAT_VERSION 1u
#define PRISM_ASSET_PACK_HEADER_BYTES 256u
#define PRISM_ASSET_PACK_NAME_MAX 31u
#define PRISM_ASSET_PATH_MAX 255u
#define PRISM_ASSET_PACK_MAX_FILES 4096u
#define PRISM_PACK_KEY_BYTES 16u

typedef uint8_t prism_pack_key_t[PRISM_PACK_KEY_BYTES];
typedef uint32_t prism_asset_pack_handle_t;

typedef enum
{
  PRISM_STORED_OBJECT_NONE = 0,
  PRISM_STORED_OBJECT_CARTRIDGE = 1,
  PRISM_STORED_OBJECT_ASSET_PACK = 2,
} prism_stored_object_kind_t;

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint16_t format_version;
  uint16_t header_size;
  uint32_t package_size;
  uint32_t version;
  uint8_t pack_key[PRISM_PACK_KEY_BYTES];
  uint8_t target_app_key[PRISM_APP_KEY_BYTES];
  uint32_t target_min_version;
  uint32_t target_max_version;
  uint32_t file_count;
  uint32_t directory_offset;
  uint32_t string_table_offset;
  uint32_t string_table_size;
  uint32_t data_offset;
  uint32_t id_offset;
  uint32_t id_length;
  uint32_t name_offset;
  uint32_t name_length;
  uint32_t target_id_offset;
  uint32_t target_id_length;
  uint8_t reserved[156];
} prism_asset_pack_header_t;

typedef struct __attribute__((packed))
{
  uint32_t path_offset;
  uint16_t path_length;
  uint16_t flags;
  uint32_t data_offset;
  uint32_t data_length;
} prism_asset_pack_file_entry_t;

typedef struct
{
  prism_asset_pack_handle_t handle;
  uint32_t version;
  uint32_t file_count;
  const char *id;
  const char *name;
} prism_asset_pack_info_t;

typedef struct
{
  const char *path;
  const void *data;
  uint32_t size;
} prism_asset_file_t;

_Static_assert(sizeof(prism_asset_pack_header_t) ==
                   PRISM_ASSET_PACK_HEADER_BYTES,
               "asset pack header is part of the file format");
_Static_assert(sizeof(prism_asset_pack_file_entry_t) == 16,
               "asset pack directory entries are part of the file format");

bool prism_asset_path_valid_n(const char *path, size_t length);
bool prism_pack_key_derive_n(const char *id, size_t length,
                             prism_pack_key_t pack_key);
bool prism_pack_key_derive(const char *id, prism_pack_key_t pack_key);
