#pragma once

#include <stdint.h>

#include <prism/cartridge.h>
#include <prism/cartridge_identity.h>
#include <prism/management_protocol.h>

#define PRISM_PACKAGE_MAGIC 0x4b505250u /* PRPK */
#define PRISM_PACKAGE_FORMAT_VERSION 1u
#define PRISM_PACKAGE_HEADER_BYTES 256u
#define PRISM_PACKAGE_MAX_RELOCATIONS 1024u
#define PRISM_PACKAGE_MAX_IMPORTS 256u
#define PRISM_PACKAGE_MAX_RW_BYTES (64u * 1024u)
#define PRISM_U8G2_ABI_HASH 0x1f40a6d9u

typedef struct __attribute__((packed))
{
  /* Package-relative word copied into RAM and rebased at launch. Relocations
   * may target the descriptor, GOT, or initialized writable-data template. */
  uint32_t patch_offset;
} prism_package_relocation_t;

typedef struct __attribute__((packed))
{
  /* Package-relative GOT word replaced with the corresponding OS export. */
  uint32_t patch_offset;
  uint16_t symbol;
  uint16_t reserved;
} prism_package_import_t;

typedef enum
{
#define PRISM_IMPORT(id, name, linker, kind, resolver) PRISM_IMPORT_##name = id,
#include <prism/imports.def>
#undef PRISM_IMPORT
} prism_package_import_symbol_t;

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint16_t format_version;
  uint16_t header_size;
  uint16_t cartridge_abi;
  uint16_t tick_divider;
  uint32_t package_size;
  uint8_t app_key[PRISM_APP_KEY_BYTES];
  uint32_t persistent_size;
  uint16_t persistent_schema;
  uint16_t reserved0;
  uint32_t image_offset;
  uint32_t image_size;
  uint32_t descriptor_offset;
  uint32_t got_offset;
  uint32_t got_size;
  uint32_t got_base_offset;
  uint32_t relocations_offset;
  uint32_t relocation_count;
  uint32_t imports_offset;
  uint32_t import_count;
  uint32_t u8g2_abi_hash;
  uint32_t rw_offset;
  uint32_t rw_init_size;
  uint32_t rw_size;
  uint8_t reserved[160];
} prism_package_header_t;

_Static_assert(sizeof(prism_package_header_t) == PRISM_PACKAGE_HEADER_BYTES,
               "package header is part of the file format");
