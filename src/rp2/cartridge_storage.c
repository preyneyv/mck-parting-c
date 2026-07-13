#include "cartridge_storage.h"

#include "shared/os/bundled_package.h"
#include "cartridge_runtime_exports.h"
#include "flash_io.h"
#include "flash_layout.h"

#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <hardware/address_mapped.h>
#include <hardware/flash.h>

#include <platform/persistence.h>
#include <platform/system.h>
#include <prism/cartridge_identity.h>
#include <prism/package.h>
#include <prism/registry.h>
#include <qrcodegen.h>
#include <shared/audio/song.h>
#include <shared/audio/synth_internal.h>
#include <shared/os/cartridge_image.h>
#include <u8g2.h>

#define CARTRIDGE_SECTORS_PER_BLOCK                                      \
  (PRISM_CARTRIDGE_BLOCK_BYTES / FLASH_SECTOR_SIZE)
#define CARTRIDGE_SECTOR_COUNT                                           \
  (PRISM_CARTRIDGE_BLOCK_COUNT * CARTRIDGE_SECTORS_PER_BLOCK)
#define MOVE_JOURNAL_PAGES_PER_SECTOR (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)
#define MOVE_JOURNAL_PAGE_COUNT                                          \
  (PRISM_FLASH_MOVE_JOURNAL_SECTORS * MOVE_JOURNAL_PAGES_PER_SECTOR)
#define CATALOG_MAGIC 0x54414350u /* PCAT */
#define CATALOG_VERSION 1u
#define CATALOG_MAX_ENTRIES 120u
#define CATALOG_MAX_LIVE 32u
#define ENTRY_LIVE 1u
#define ENTRY_DEAD 2u
#define MOVE_MAGIC 0x314f4d50u /* PMO1 */
#define MOVE_NONE 0u
#define MOVE_BEGIN 1u
#define MOVE_SCRATCH_READY 2u
#define MOVE_DEST_READY 3u
#define TRACE_INSTALL_BEGIN 0x49424547u /* IBEG */
#define TRACE_INSTALL_CHUNK 0x4943484bu /* ICHK */
#define TRACE_INSTALL_COMMIT 0x49434f4du /* ICOM */
#define TRACE_DELETE 0x44454c45u /* DELE */
#define TRACE_COMPACT 0x434f4d50u /* COMP */

typedef struct __attribute__((packed))
{
  uint8_t app_key[PRISM_APP_KEY_BYTES];
  uint16_t start_block;
  uint16_t block_count;
  uint32_t package_bytes;
  uint32_t package_crc32;
  uint8_t state;
  uint8_t reserved[3];
} catalog_entry_t;

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint16_t version;
  uint16_t entry_count;
  uint32_t generation;
  uint32_t flags;
  uint32_t entries_crc32;
  catalog_entry_t entries[CATALOG_MAX_ENTRIES];
} catalog_t;

_Static_assert(sizeof(catalog_t) <= FLASH_SECTOR_SIZE,
               "cartridge catalog must fit in one flash sector");

typedef struct
{
  bool active;
  prism_management_install_begin_t begin;
  uint16_t start_block;
  uint32_t received;
  uint32_t crc;
  int16_t replacement_slot;
  bool erased[CARTRIDGE_SECTOR_COUNT];
} install_session_t;

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint32_t generation;
  uint8_t stage;
  uint8_t scratch_index;
  uint16_t entry_index;
  uint16_t old_start;
  uint16_t target_start;
  uint16_t total_blocks;
  uint16_t next_block;
  uint32_t block_crc32;
  uint32_t journal_sector_erases[PRISM_FLASH_MOVE_JOURNAL_SECTORS];
  uint32_t scratch_block_uses[PRISM_FLASH_SCRATCH_POOL_COUNT];
  uint32_t crc32;
} move_record_t;

_Static_assert(sizeof(move_record_t) <= FLASH_PAGE_SIZE,
               "move journal records must fit one flash page");
static catalog_t catalog;
static uint8_t catalog_slot;
static install_session_t installation;
static uint8_t catalog_sector[FLASH_SECTOR_SIZE];
static move_record_t move_record;
static uint8_t move_journal_sector;
static uint8_t move_journal_page;
static bool move_journal_location_valid;
static uint8_t move_page[FLASH_PAGE_SIZE];
static uint16_t allocation_cursor;
/* Core 0 has a 2 KiB stack.  Keep compaction's ordering workspace in BSS;
 * even this small index array should not compete with the management call
 * stack while flash operations are in progress. */
static uint16_t compaction_indices[CATALOG_MAX_ENTRIES];
static uint8_t default_install_chunk[sizeof(prism_management_install_chunk_t) +
                                     1024u];
static uint8_t install_program_buffer[1024u];

extern const uint8_t prism_default_bongocat_start[];
extern const uint8_t prism_default_bongocat_end[];
extern const uint8_t prism_default_morse_start[];
extern const uint8_t prism_default_morse_end[];
extern const uint8_t prism_default_asteroids_start[];
extern const uint8_t prism_default_asteroids_end[];
extern const uint8_t prism_default_beatline_start[];
extern const uint8_t prism_default_beatline_end[];

typedef struct
{
  bool valid;
  prism_cartridge_t descriptor;
} runtime_descriptor_t;

static runtime_descriptor_t runtime_descriptors[CATALOG_MAX_ENTRIES];

static const prism_cartridge_t *load_descriptor(uint16_t slot);
static void reconcile_catalog(void);
static bool catalog_save(void);

typedef struct
{
  const uint8_t *start;
  const uint8_t *end;
} default_cartridge_t;

static const default_cartridge_t default_cartridges[] = {
    {prism_default_bongocat_start, prism_default_bongocat_end},
    {prism_default_morse_start, prism_default_morse_end},
    {prism_default_asteroids_start, prism_default_asteroids_end},
    {prism_default_beatline_start, prism_default_beatline_end},
};

static void trace_set(uint32_t stage, uint32_t detail)
{
  platform_watchdog_trace(stage, detail);
}

static void trace_clear(void)
{
  platform_watchdog_trace(0, 0);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
  for (size_t i = 0; i < size; ++i)
  {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
  }
  return crc;
}

static uint32_t crc32(const void *data, size_t size)
{
  return crc32_update(UINT32_MAX, data, size) ^ UINT32_MAX;
}

static bool range_valid(uint32_t offset, uint32_t size, uint32_t limit)
{
  return offset <= limit && size <= limit - offset;
}

static const uint8_t *entry_package(const catalog_entry_t *entry)
{
  return (const uint8_t *)XIP_BASE + PRISM_FLASH_CARTRIDGE_OFFSET +
         entry->start_block * PRISM_CARTRIDGE_BLOCK_BYTES;
}

static uintptr_t resolve_import(uint16_t symbol);

static uint32_t resolve_launch_import(uint16_t symbol, void *user)
{
  (void)user;
  return (uint32_t)resolve_import(symbol);
}

static bool package_metadata(const uint8_t *package,
                             const prism_package_header_t *header,
                             const prism_cartridge_t **descriptor_out,
                             const char **id_out, const char **name_out)
{
  const prism_cartridge_t *descriptor =
      (const void *)(package + header->descriptor_offset);
  uint32_t id_offset = (uint32_t)(uintptr_t)descriptor->id;
  uint32_t name_offset = (uint32_t)(uintptr_t)descriptor->name;
  uint32_t icon_offset = (uint32_t)(uintptr_t)descriptor->icon;
  uint32_t frame_offset = (uint32_t)(uintptr_t)descriptor->frame;
  if (descriptor->magic != PRISM_CARTRIDGE_MAGIC ||
      descriptor->abi_version != PRISM_CARTRIDGE_ABI_VERSION ||
      descriptor->descriptor_size != sizeof(*descriptor) ||
      descriptor->tick_divider != header->tick_divider ||
      descriptor->persistent_size != header->persistent_size ||
      descriptor->persistent_schema_version != header->persistent_schema ||
      id_offset == 0 || name_offset == 0 || icon_offset == 0 ||
      frame_offset == 0 || (frame_offset & 1u) == 0 ||
      !range_valid(id_offset, 1, header->image_size) ||
      !range_valid(name_offset, 1, header->image_size) ||
      !range_valid(icon_offset, PRISM_CARTRIDGE_ICON_BYTES,
                   header->image_size) ||
      !range_valid(frame_offset & ~1u, 2, header->image_size))
    return false;
  const char *id = (const char *)(package + header->image_offset + id_offset);
  const char *name =
      (const char *)(package + header->image_offset + name_offset);
  const char *id_end = memchr(id, '\0', header->image_size - id_offset);
  const char *name_end = memchr(name, '\0', header->image_size - name_offset);
  prism_app_key_t derived_key;
  if (id_end == NULL || name_end == NULL || name_end == name ||
      (size_t)(name_end - name) > PRISM_CARTRIDGE_NAME_MAX ||
      !prism_app_key_derive_n(id, (size_t)(id_end - id), derived_key) ||
      memcmp(derived_key, header->app_key, sizeof(derived_key)) != 0)
    return false;
  if (descriptor_out != NULL)
    *descriptor_out = descriptor;
  if (id_out != NULL)
    *id_out = id;
  if (name_out != NULL)
    *name_out = name;
  return true;
}

static bool package_valid(const uint8_t *package, uint32_t allocated_bytes)
{
  if (package == NULL || allocated_bytes < sizeof(prism_package_header_t))
    return false;
  const prism_package_header_t *header = (const void *)package;
  if (header->magic != PRISM_PACKAGE_MAGIC ||
      header->format_version != PRISM_PACKAGE_FORMAT_VERSION ||
      header->header_size != sizeof(*header) ||
      header->cartridge_abi != PRISM_CARTRIDGE_ABI_VERSION ||
      header->tick_divider == 0 ||
      header->package_size < sizeof(*header) ||
      header->package_size > allocated_bytes ||
      header->u8g2_abi_hash != PRISM_U8G2_ABI_HASH ||
      header->relocation_count > PRISM_PACKAGE_MAX_RELOCATIONS ||
      header->import_count > PRISM_PACKAGE_MAX_IMPORTS)
    return false;

  if (!range_valid(header->image_offset, header->image_size,
                   header->package_size) ||
      !range_valid(header->descriptor_offset, sizeof(prism_cartridge_t),
                   header->package_size) ||
      !range_valid(header->got_offset, header->got_size,
                   header->package_size) ||
      (header->got_base_offset & (sizeof(uint32_t) - 1u)) != 0 ||
      (header->got_size == 0 ? header->got_base_offset != 0
                             : header->got_base_offset >= header->got_size) ||
      !range_valid(header->relocations_offset,
                   header->relocation_count *
                       sizeof(prism_package_relocation_t),
                   header->package_size) ||
      !range_valid(header->imports_offset,
                   header->import_count * sizeof(prism_package_import_t),
                   header->package_size) ||
      !range_valid(header->rw_offset, header->rw_init_size,
                   header->package_size) ||
      header->rw_init_size > header->rw_size ||
      header->rw_size > PRISM_PACKAGE_MAX_RW_BYTES)
    return false;

  uint32_t image_end = header->image_offset + header->image_size;
  uint32_t descriptor_end =
      header->descriptor_offset + sizeof(prism_cartridge_t);
  uint32_t got_end = header->got_offset + header->got_size;
  uint32_t rw_init_end = header->rw_offset + header->rw_init_size;
  if (header->descriptor_offset < header->image_offset ||
      descriptor_end > image_end || header->got_offset < header->image_offset ||
      got_end > image_end || (header->got_size & 3u) != 0 ||
      header->rw_offset < image_end)
    return false;

  if (!package_metadata(package, header, NULL, NULL, NULL))
    return false;

  const prism_package_relocation_t *relocations =
      (const void *)(package + header->relocations_offset);
  for (uint32_t i = 0; i < header->relocation_count; ++i)
  {
    uint32_t patch = relocations[i].patch_offset;
    if ((patch & 3u) != 0 || !range_valid(patch, sizeof(uint32_t),
                                          header->package_size) ||
        !((patch >= header->descriptor_offset && patch < descriptor_end) ||
          (patch >= header->got_offset && patch < got_end) ||
          (patch >= header->rw_offset && patch < rw_init_end)))
      return false;
    uint32_t value;
    memcpy(&value, package + patch, sizeof(value));
    if ((value & ~1u) >= header->image_size + header->rw_size)
      return false;
  }

  const prism_package_import_t *imports =
      (const void *)(package + header->imports_offset);
  for (uint32_t i = 0; i < header->import_count; ++i)
    if (imports[i].reserved != 0 || imports[i].symbol == 0 ||
        resolve_import(imports[i].symbol) == 0 ||
        (imports[i].patch_offset & 3u) != 0 ||
        imports[i].patch_offset < header->got_offset ||
        !range_valid(imports[i].patch_offset, sizeof(uint32_t), got_end))
      return false;
  return true;
}

#define IMPORT_ADDRESS(symbol) ((uintptr_t)(void *)&(symbol))
#define PRISM_RESOLVE_FUNCTION(symbol) IMPORT_ADDRESS(symbol)
#define PRISM_RESOLVE_OBJECT(symbol) ((uintptr_t)(symbol))

static uintptr_t resolve_import(uint16_t symbol)
{
  switch ((prism_package_import_symbol_t)symbol)
  {
#define PRISM_IMPORT(id, name, linker, kind, resolver)                        \
  case PRISM_IMPORT_##name: return PRISM_RESOLVE_##kind(resolver);
#include <prism/imports.def>
#undef PRISM_IMPORT
  default: return 0;
  }
}

static uint32_t catalog_offset(uint8_t slot)
{
  return slot ? PRISM_FLASH_CATALOG1_OFFSET : PRISM_FLASH_CATALOG0_OFFSET;
}

static bool catalog_valid(const catalog_t *value)
{
  return value->magic == CATALOG_MAGIC && value->version == CATALOG_VERSION &&
         value->entry_count <= CATALOG_MAX_ENTRIES &&
         value->entries_crc32 ==
             crc32(value->entries,
                   value->entry_count * sizeof(catalog_entry_t));
}

static int catalog_find_app_key(
    const uint8_t app_key[PRISM_APP_KEY_BYTES])
{
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
    if (catalog.entries[i].state == ENTRY_LIVE &&
        memcmp(catalog.entries[i].app_key, app_key, PRISM_APP_KEY_BYTES) == 0)
      return (int)i;
  return -1;
}

static bool seed_default_cartridge(const default_cartridge_t *value)
{
  size_t size = (size_t)(value->end - value->start);
  if (size < sizeof(prism_package_header_t) || size > UINT32_MAX)
    return false;
  const prism_package_header_t *header = (const void *)value->start;
  const prism_cartridge_t *bundled_descriptor = NULL;
  if (!package_valid(value->start, (uint32_t)size) ||
      header->package_size != size ||
      !package_metadata(value->start, header, &bundled_descriptor, NULL,
                        NULL))
    return false;

  uint32_t package_crc32 = crc32(value->start, size);
  int installed_slot = catalog_find_app_key(header->app_key);
  if (installed_slot >= 0)
  {
    const prism_cartridge_t *installed_descriptor =
        load_descriptor((uint16_t)installed_slot);
    if (installed_descriptor == NULL)
      return false;
    const catalog_entry_t *installed = &catalog.entries[installed_slot];
    if (!prism_bundled_package_should_replace(
            installed_descriptor->version, installed->package_bytes,
            installed->package_crc32, bundled_descriptor->version,
            (uint32_t)size, package_crc32))
      return true;
  }

  prism_management_install_begin_t begin = {0};
  memcpy(begin.app_key, header->app_key, sizeof(begin.app_key));
  begin.package_bytes = (uint32_t)size;
  begin.package_crc32 = package_crc32;
  begin.required_blocks = (uint16_t)((size + PRISM_CARTRIDGE_BLOCK_BYTES - 1u) /
                                     PRISM_CARTRIDGE_BLOCK_BYTES);
  if (cartridge_storage_install_begin(&begin) != PRISM_MGMT_OK)
    return false;

  for (uint32_t offset = 0; offset < size; offset += 1024u)
  {
    prism_management_install_chunk_t *chunk =
        (void *)default_install_chunk;
    uint32_t remaining = (uint32_t)size - offset;
    uint16_t length = (uint16_t)(remaining > 1024u ? 1024u : remaining);
    chunk->offset = offset;
    chunk->data_len = length;
    chunk->reserved = 0;
    memcpy(chunk->data, value->start + offset, length);
    if (cartridge_storage_install_chunk(
            chunk, sizeof(prism_management_install_chunk_t) + length) !=
        PRISM_MGMT_OK)
    {
      cartridge_storage_install_abort();
      return false;
    }
  }
  if (cartridge_storage_install_commit() != PRISM_MGMT_OK)
  {
    cartridge_storage_install_abort();
    return false;
  }
  return true;
}

static void seed_default_cartridges(void)
{
  for (size_t i = 0;
       i < sizeof(default_cartridges) / sizeof(default_cartridges[0]); ++i)
    if (!seed_default_cartridge(&default_cartridges[i]))
      return;
}

static bool catalog_save(void)
{
  catalog.generation++;
  catalog.entries_crc32 =
      crc32(catalog.entries, catalog.entry_count * sizeof(catalog_entry_t));
  memset(catalog_sector, 0xff, sizeof(catalog_sector));
  memcpy(catalog_sector, &catalog, sizeof(catalog));
  uint8_t next_slot = catalog_slot ^ 1u;
  uint32_t offset = catalog_offset(next_slot);
  if (!prism_flash_erase(offset, FLASH_SECTOR_SIZE) ||
      !prism_flash_program(offset, catalog_sector, FLASH_SECTOR_SIZE))
    return false;
  catalog_slot = next_slot;
  return true;
}

static uint32_t move_sector_offset(uint8_t sector)
{
  return PRISM_FLASH_MOVE_JOURNAL_OFFSET + sector * FLASH_SECTOR_SIZE;
}

static uint32_t move_page_offset(uint8_t sector, uint8_t page)
{
  return move_sector_offset(sector) + page * FLASH_PAGE_SIZE;
}

static uint32_t move_crc(const move_record_t *record)
{
  return crc32(record, offsetof(move_record_t, crc32));
}

static bool move_valid(const move_record_t *record)
{
  return record->magic == MOVE_MAGIC && record->stage <= MOVE_DEST_READY &&
         record->scratch_index < PRISM_FLASH_SCRATCH_POOL_COUNT &&
         record->crc32 == move_crc(record);
}

static bool flash_page_erased(uint32_t offset)
{
  const uint8_t *page = (const uint8_t *)XIP_BASE + offset;
  for (size_t i = 0; i < FLASH_PAGE_SIZE; ++i)
    if (page[i] != 0xff)
      return false;
  return true;
}

static bool move_save(void)
{
  move_record.magic = MOVE_MAGIC;
  move_record.generation++;
  uint8_t target_sector = 0;
  uint8_t target_page = 0;
  bool found = false;
  uint16_t current = move_journal_location_valid
                         ? (uint16_t)(move_journal_sector *
                                          MOVE_JOURNAL_PAGES_PER_SECTOR +
                                      move_journal_page)
                         : (uint16_t)(MOVE_JOURNAL_PAGE_COUNT - 1u);
  for (uint16_t distance = 1; distance <= MOVE_JOURNAL_PAGE_COUNT;
       ++distance)
  {
    uint16_t candidate = (uint16_t)((current + distance) %
                                    MOVE_JOURNAL_PAGE_COUNT);
    uint8_t sector =
        (uint8_t)(candidate / MOVE_JOURNAL_PAGES_PER_SECTOR);
    uint8_t page = (uint8_t)(candidate % MOVE_JOURNAL_PAGES_PER_SECTOR);
    if (flash_page_erased(move_page_offset(sector, page)))
    {
      target_sector = sector;
      target_page = page;
      found = true;
      break;
    }
  }
  if (!found)
  {
    target_sector = move_journal_location_valid
                        ? (uint8_t)((move_journal_sector + 1u) %
                                    PRISM_FLASH_MOVE_JOURNAL_SECTORS)
                        : 0;
    if (!prism_flash_erase(move_sector_offset(target_sector),
                           FLASH_SECTOR_SIZE))
      return false;
    ++move_record.journal_sector_erases[target_sector];
    target_page = 0;
  }

  move_record.crc32 = move_crc(&move_record);
  memset(move_page, 0xff, sizeof(move_page));
  memcpy(move_page, &move_record, sizeof(move_record));
  if (!prism_flash_program(move_page_offset(target_sector, target_page),
                           move_page, sizeof(move_page)))
    return false;
  move_journal_sector = target_sector;
  move_journal_page = target_page;
  move_journal_location_valid = true;
  return true;
}

static uint32_t scratch_offset(uint8_t index)
{
  if (index == 0)
    return PRISM_FLASH_COMPACTION_SCRATCH_OFFSET;
  return PRISM_FLASH_EXTRA_SCRATCH_OFFSET +
         (index - 1u) * PRISM_CARTRIDGE_BLOCK_BYTES;
}

static uint32_t block_flash_offset(uint16_t block)
{
  return PRISM_FLASH_CARTRIDGE_OFFSET + block * PRISM_CARTRIDGE_BLOCK_BYTES;
}

static uint32_t block_crc(uint32_t flash_offset)
{
  return crc32((const uint8_t *)XIP_BASE + flash_offset,
               PRISM_CARTRIDGE_BLOCK_BYTES);
}

static bool erase_block_at(uint32_t flash_offset)
{
  for (uint32_t sector = 0; sector < PRISM_CARTRIDGE_BLOCK_BYTES;
       sector += FLASH_SECTOR_SIZE)
    if (!prism_flash_erase(flash_offset + sector, FLASH_SECTOR_SIZE))
      return false;
  return true;
}

static bool copy_block(uint32_t source_offset, uint32_t destination_offset)
{
  if (!erase_block_at(destination_offset))
    return false;
  uint8_t page[FLASH_PAGE_SIZE];
  for (uint32_t offset = 0; offset < PRISM_CARTRIDGE_BLOCK_BYTES;
       offset += FLASH_PAGE_SIZE)
  {
    memcpy(page, (const uint8_t *)XIP_BASE + source_offset + offset,
           sizeof(page));
    if (!prism_flash_program(destination_offset + offset, page, sizeof(page)))
      return false;
  }
  return true;
}

static bool recover_move(cartridge_storage_progress_fn progress, void *user,
                         uint16_t *completed, uint16_t total)
{
  while (move_record.stage != MOVE_NONE)
  {
    if (move_record.entry_index >= catalog.entry_count)
      return false;
    catalog_entry_t *entry = &catalog.entries[move_record.entry_index];
    if (entry->state != ENTRY_LIVE)
      return false;
    if (entry->start_block == move_record.target_start)
    {
      move_record.stage = MOVE_NONE;
      return move_save();
    }
    if (move_record.next_block >= move_record.total_blocks)
    {
      entry->start_block = move_record.target_start;
      if (!catalog_save())
        return false;
      move_record.stage = MOVE_NONE;
      return move_save();
    }

    uint16_t source_block = move_record.old_start + move_record.next_block;
    uint16_t destination_block =
        move_record.target_start + move_record.next_block;
    uint32_t source = block_flash_offset(source_block);
    uint32_t destination = block_flash_offset(destination_block);
    uint32_t scratch = scratch_offset(move_record.scratch_index);

    if (move_record.stage == MOVE_BEGIN)
    {
      if (!copy_block(source, scratch))
        return false;
      move_record.block_crc32 = block_crc(scratch);
      if (move_record.block_crc32 != block_crc(source))
        return false;
      move_record.stage = MOVE_SCRATCH_READY;
      if (!move_save())
        return false;
    }
    if (move_record.stage == MOVE_SCRATCH_READY)
    {
      if (block_crc(scratch) != move_record.block_crc32 &&
          !copy_block(source, scratch))
        return false;
      if (!copy_block(scratch, destination) ||
          block_crc(destination) != move_record.block_crc32)
        return false;
      move_record.stage = MOVE_DEST_READY;
      if (!move_save())
        return false;
    }
    if (move_record.stage == MOVE_DEST_READY)
    {
      if (block_crc(destination) != move_record.block_crc32)
      {
        if (block_crc(scratch) != move_record.block_crc32 ||
            !copy_block(scratch, destination))
          return false;
      }
      move_record.next_block++;
      if (progress != NULL && completed != NULL)
      {
        ++*completed;
        progress(*completed, total, user);
      }
      move_record.stage = MOVE_BEGIN;
      if (move_record.next_block < move_record.total_blocks)
      {
        move_record.scratch_index =
            (uint8_t)((move_record.scratch_index + 1u) %
                      PRISM_FLASH_SCRATCH_POOL_COUNT);
        ++move_record.scratch_block_uses[move_record.scratch_index];
      }
      if (!move_save())
        return false;
    }
  }
  return true;
}

void cartridge_storage_init(void)
{
  const catalog_t *slot0 =
      (const void *)((const uint8_t *)XIP_BASE + PRISM_FLASH_CATALOG0_OFFSET);
  const catalog_t *slot1 =
      (const void *)((const uint8_t *)XIP_BASE + PRISM_FLASH_CATALOG1_OFFSET);
  bool valid0 = catalog_valid(slot0);
  bool valid1 = catalog_valid(slot1);
  memset(&catalog, 0, sizeof(catalog));
  catalog.magic = CATALOG_MAGIC;
  catalog.version = CATALOG_VERSION;
  if (valid1 && (!valid0 ||
                 (int32_t)(slot1->generation - slot0->generation) > 0))
  {
    catalog = *slot1;
    catalog_slot = 1;
  }
  else if (valid0)
  {
    catalog = *slot0;
    catalog_slot = 0;
  }
  memset(&installation, 0, sizeof(installation));
  memset(runtime_descriptors, 0, sizeof(runtime_descriptors));

  memset(&move_record, 0, sizeof(move_record));
  move_record.magic = MOVE_MAGIC;
  move_journal_location_valid = false;
  for (uint8_t sector = 0; sector < PRISM_FLASH_MOVE_JOURNAL_SECTORS; ++sector)
    for (uint8_t page = 0; page < MOVE_JOURNAL_PAGES_PER_SECTOR; ++page)
    {
      const uint8_t *address =
          (const uint8_t *)XIP_BASE + move_page_offset(sector, page);
      move_record_t candidate = {0};
      const move_record_t *current_record = (const void *)address;
      if (!move_valid(current_record))
        continue;
      candidate = *current_record;
      if (!move_journal_location_valid ||
          (int32_t)(candidate.generation - move_record.generation) > 0)
      {
        move_record = candidate;
        move_journal_sector = sector;
        move_journal_page = page;
        move_journal_location_valid = true;
      }
    }
  allocation_cursor =
      (uint16_t)(catalog.generation % PRISM_CARTRIDGE_BLOCK_COUNT);
  recover_move(NULL, NULL, NULL, 0);
  reconcile_catalog();
  seed_default_cartridges();
}

static void block_map(uint8_t map[PRISM_CARTRIDGE_BLOCK_COUNT])
{
  memset(map, 0, PRISM_CARTRIDGE_BLOCK_COUNT);
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
  {
    const catalog_entry_t *entry = &catalog.entries[i];
    if (entry->state != ENTRY_LIVE)
      continue;
    for (uint16_t block = entry->start_block;
         block < entry->start_block + entry->block_count &&
         block < PRISM_CARTRIDGE_BLOCK_COUNT;
         ++block)
      map[block] = ENTRY_LIVE;
  }
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
  {
    const catalog_entry_t *entry = &catalog.entries[i];
    if (entry->state != ENTRY_DEAD)
      continue;
    for (uint16_t block = entry->start_block;
         block < entry->start_block + entry->block_count &&
         block < PRISM_CARTRIDGE_BLOCK_COUNT;
         ++block)
      if (map[block] == 0)
        map[block] = ENTRY_DEAD;
  }
}

void cartridge_storage_info(prism_management_storage_info_t *info)
{
  memset(info, 0, sizeof(*info));
  block_map(info->block_states);
  info->total_blocks = PRISM_CARTRIDGE_BLOCK_COUNT;
  info->scratch_blocks = PRISM_FLASH_SCRATCH_POOL_COUNT;
  info->required_blocks = installation.active ? installation.begin.required_blocks : 0;
  uint16_t free_run = 0, reclaimable_run = 0;
  for (uint16_t block = 0; block < PRISM_CARTRIDGE_BLOCK_COUNT; ++block)
  {
    if (info->block_states[block] == ENTRY_LIVE)
    {
      info->live_blocks++;
      free_run = reclaimable_run = 0;
    }
    else
    {
      ++reclaimable_run;
      if (reclaimable_run > info->largest_reclaimable_run)
        info->largest_reclaimable_run = reclaimable_run;
      if (info->block_states[block] == ENTRY_DEAD)
      {
        info->dead_blocks++;
        free_run = 0;
      }
      else
      {
        info->erased_blocks++;
        ++free_run;
        if (free_run > info->largest_free_run)
          info->largest_free_run = free_run;
      }
    }
  }
}

static bool code_pointer_in_image(const void *pointer, const uint8_t *image,
                                  uint32_t image_size)
{
  if (pointer == NULL)
    return true;
  uintptr_t value = (uintptr_t)pointer;
  if ((value & 1u) == 0)
    return false;
  value &= ~1u;
  return value >= (uintptr_t)image &&
         value < (uintptr_t)image + image_size;
}

static bool string_in_image(const char *string, const uint8_t *image,
                            uint32_t image_size)
{
  if (string == NULL)
    return false;
  uintptr_t value = (uintptr_t)string;
  uintptr_t start = (uintptr_t)image;
  if (value < start || value - start >= image_size)
    return false;
  return memchr(string, '\0', image_size - (value - start)) != NULL;
}

static bool bytes_in_image(const void *pointer, uint32_t size,
                           const uint8_t *image, uint32_t image_size)
{
  if (pointer == NULL)
    return true;
  uintptr_t value = (uintptr_t)pointer;
  uintptr_t start = (uintptr_t)image;
  return value >= start && value - start <= image_size &&
         size <= image_size - (value - start);
}

static const prism_cartridge_t *load_descriptor(uint16_t slot)
{
  if (slot >= catalog.entry_count || catalog.entries[slot].state != ENTRY_LIVE)
    return NULL;
  runtime_descriptor_t *runtime = &runtime_descriptors[slot];
  if (runtime->valid)
    return &runtime->descriptor;

  const catalog_entry_t *entry = &catalog.entries[slot];
  if (entry->block_count == 0 ||
      entry->start_block >= PRISM_CARTRIDGE_BLOCK_COUNT ||
      entry->block_count >
          PRISM_CARTRIDGE_BLOCK_COUNT - entry->start_block)
    return NULL;
  const uint8_t *package = entry_package(entry);
  uint32_t allocated = entry->block_count * PRISM_CARTRIDGE_BLOCK_BYTES;
  if (!package_valid(package, allocated))
    return NULL;
  const prism_package_header_t *header = (const void *)package;
  if (header->package_size != entry->package_bytes ||
      memcmp(header->app_key, entry->app_key, sizeof(entry->app_key)) != 0)
    return NULL;
  const uint8_t *image = package + header->image_offset;
  memcpy(&runtime->descriptor, package + header->descriptor_offset,
         sizeof(runtime->descriptor));

  const prism_package_relocation_t *relocations =
      (const void *)(package + header->relocations_offset);
  uint32_t descriptor_end =
      header->descriptor_offset + sizeof(runtime->descriptor);
  for (uint32_t i = 0; i < header->relocation_count; ++i)
  {
    uint32_t patch = relocations[i].patch_offset;
    if (patch < header->descriptor_offset || patch >= descriptor_end)
      continue;
    uint32_t *word = (void *)((uint8_t *)&runtime->descriptor +
                              patch - header->descriptor_offset);
    *word += (uint32_t)(uintptr_t)image;
  }

  const prism_cartridge_t *descriptor = &runtime->descriptor;
  if (descriptor->magic != PRISM_CARTRIDGE_MAGIC ||
      descriptor->abi_version != PRISM_CARTRIDGE_ABI_VERSION ||
      descriptor->descriptor_size != sizeof(*descriptor) ||
      descriptor->tick_divider != header->tick_divider ||
      descriptor->persistent_size != header->persistent_size ||
      descriptor->persistent_schema_version != header->persistent_schema ||
      !string_in_image(descriptor->id, image, header->image_size) ||
      !string_in_image(descriptor->name, image, header->image_size) ||
      !bytes_in_image(descriptor->icon, PRISM_CARTRIDGE_ICON_BYTES, image,
                      header->image_size) ||
      !code_pointer_in_image((const void *)descriptor->enter, image,
                             header->image_size) ||
      !code_pointer_in_image((const void *)descriptor->tick, image,
                             header->image_size) ||
      !code_pointer_in_image((const void *)descriptor->frame, image,
                             header->image_size) ||
      !code_pointer_in_image((const void *)descriptor->pause, image,
                             header->image_size) ||
      !code_pointer_in_image((const void *)descriptor->resume, image,
                             header->image_size) ||
      !code_pointer_in_image((const void *)descriptor->leave, image,
                             header->image_size))
  {
    memset(runtime, 0, sizeof(*runtime));
    return NULL;
  }
  prism_app_key_t derived_key;
  if (!prism_app_key_derive(descriptor->id, derived_key) ||
      memcmp(derived_key, header->app_key, sizeof(derived_key)) != 0)
  {
    memset(runtime, 0, sizeof(*runtime));
    return NULL;
  }
  runtime->valid = true;
  return descriptor;
}

/* The catalog is the source of block ownership, while the launcher only
 * exposes packages that pass the complete package and descriptor checks.
 * Reconcile the two at boot so interrupted or obsolete installs cannot hide
 * blocks in a live-but-unlaunchable state.  Their blocks remain reclaimable
 * until the next compaction. */
static void reconcile_catalog(void)
{
  bool changed = false;
  for (uint16_t slot = 0; slot < catalog.entry_count; ++slot)
  {
    if (catalog.entries[slot].state != ENTRY_LIVE ||
        load_descriptor(slot) != NULL)
      continue;
    catalog.entries[slot].state = ENTRY_DEAD;
    memset(&runtime_descriptors[slot], 0, sizeof(runtime_descriptors[slot]));
    changed = true;
  }
  if (changed)
    catalog_save();
}

static int installed_slot(size_t index)
{
  for (uint16_t slot = 0; slot < catalog.entry_count; ++slot)
    if (catalog.entries[slot].state == ENTRY_LIVE &&
        load_descriptor(slot) != NULL && index-- == 0)
      return slot;
  return -1;
}

size_t cartridge_storage_installed_count(void)
{
  size_t count = 0;
  for (uint16_t slot = 0; slot < catalog.entry_count; ++slot)
    if (catalog.entries[slot].state == ENTRY_LIVE &&
        load_descriptor(slot) != NULL)
      ++count;
  return count;
}

size_t cartridge_storage_count(void)
{
  return cartridge_storage_installed_count();
}

const prism_cartridge_t *cartridge_storage_installed_get(size_t index)
{
  int slot = installed_slot(index);
  return slot < 0 ? NULL : load_descriptor((uint16_t)slot);
}

const prism_cartridge_t *cartridge_storage_find_app_key(
    const uint8_t app_key[PRISM_APP_KEY_BYTES])
{
  if (app_key == NULL)
    return NULL;
  for (uint16_t slot = 0; slot < catalog.entry_count; ++slot)
    if (catalog.entries[slot].state == ENTRY_LIVE &&
        memcmp(catalog.entries[slot].app_key, app_key,
               PRISM_APP_KEY_BYTES) == 0)
      return load_descriptor(slot);
  return NULL;
}

bool cartridge_storage_owns(const prism_cartridge_t *cartridge)
{
  if (cartridge == NULL)
    return false;
  for (uint16_t slot = 0; slot < catalog.entry_count; ++slot)
    if (catalog.entries[slot].state == ENTRY_LIVE &&
        load_descriptor(slot) == cartridge)
      return true;
  return false;
}

bool cartridge_storage_prepare(const prism_cartridge_t *cartridge,
                               platform_cartridge_execution_t *execution)
{
  if (execution == NULL)
    return false;
  int owner = -1;
  for (uint16_t slot = 0; slot < catalog.entry_count; ++slot)
    if (catalog.entries[slot].state == ENTRY_LIVE &&
        load_descriptor(slot) == cartridge)
    {
      owner = slot;
      break;
    }
  if (owner < 0)
    return false;

  const catalog_entry_t *entry = &catalog.entries[owner];
  const uint8_t *package = entry_package(entry);
  const prism_package_header_t *header = (const void *)package;
  const uint8_t *image = package + header->image_offset;
  if (header->got_size == 0 && header->rw_size == 0)
    return true;

  size_t allocation_size = header->got_size + header->rw_size;
  uint8_t *allocation = malloc(allocation_size);
  if (allocation == NULL)
  {
    printf("cartridge launch rejected: need %lu bytes (%lu GOT + %lu RW/BSS)\n",
           (unsigned long)allocation_size,
           (unsigned long)header->got_size,
           (unsigned long)header->rw_size);
    return false;
  }
  uint8_t *got = allocation;
  uint8_t *rw = allocation + header->got_size;
  if (!prism_package_prepare_launch_image(
          package, header, (uint32_t)(uintptr_t)image, got,
          (uint32_t)(uintptr_t)rw, rw, resolve_launch_import, NULL))
  {
    free(allocation);
    return false;
  }

  execution->allocation = allocation;
  execution->got_base =
      header->got_size > 0 ? got + header->got_base_offset : NULL;
  return true;
}

static const catalog_entry_t *live_entry(size_t index)
{
  int slot = installed_slot(index);
  return slot < 0 ? NULL : &catalog.entries[slot];
}

bool cartridge_storage_entry(size_t index,
                             prism_management_cartridge_entry_t *entry,
                             const char **id, const char **name)
{
  const catalog_entry_t *stored = live_entry(index);
  if (stored == NULL)
    return false;
  const prism_package_header_t *header =
      (const void *)((const uint8_t *)XIP_BASE + PRISM_FLASH_CARTRIDGE_OFFSET +
                     stored->start_block * PRISM_CARTRIDGE_BLOCK_BYTES);
  if (header->magic != PRISM_PACKAGE_MAGIC)
    return false;
  const prism_cartridge_t *descriptor =
      load_descriptor((uint16_t)(stored - catalog.entries));
  if (descriptor == NULL || id == NULL || name == NULL)
    return false;
  memset(entry, 0, sizeof(*entry));
  memcpy(entry->app_key, stored->app_key, sizeof(entry->app_key));
  entry->package_bytes = stored->package_bytes;
  entry->persistent_bytes = header->persistent_size;
  entry->version = descriptor->version;
  entry->blocks = stored->block_count;
  *id = descriptor->id;
  *name = descriptor->name;
  return true;
}

static bool app_key_equal(const uint8_t a[PRISM_APP_KEY_BYTES],
                          const uint8_t b[PRISM_APP_KEY_BYTES])
{
  return memcmp(a, b, PRISM_APP_KEY_BYTES) == 0;
}

static int find_run(uint16_t required)
{
  uint8_t map[PRISM_CARTRIDGE_BLOCK_COUNT];
  block_map(map);
  for (uint16_t distance = 0; distance < PRISM_CARTRIDGE_BLOCK_COUNT;
       ++distance)
  {
    uint16_t start =
        (uint16_t)((allocation_cursor + distance) %
                   PRISM_CARTRIDGE_BLOCK_COUNT);
    if (start + required > PRISM_CARTRIDGE_BLOCK_COUNT)
      continue;
    bool available = true;
    for (uint16_t block = start; block < start + required; ++block)
      if (map[block] == ENTRY_LIVE)
      {
        available = false;
        break;
      }
    if (available)
      return start;
  }
  return -1;
}

prism_management_status_t cartridge_storage_install_begin(
    const prism_management_install_begin_t *begin)
{
  trace_set(TRACE_INSTALL_BEGIN, begin != NULL ? begin->package_bytes : 0);
  if (installation.active || begin == NULL || begin->package_bytes < 256)
  {
    trace_clear();
    return PRISM_MGMT_ERROR_BUSY;
  }
  uint16_t required = (uint16_t)((begin->package_bytes +
                                  PRISM_CARTRIDGE_BLOCK_BYTES - 1u) /
                                 PRISM_CARTRIDGE_BLOCK_BYTES);
  if (required == 0 || required > PRISM_CARTRIDGE_BLOCK_COUNT ||
      begin->required_blocks != required)
  {
    trace_clear();
    return PRISM_MGMT_ERROR_BAD_MESSAGE;
  }
  int16_t replacement = -1;
  uint16_t live_count = 0;
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
    if (catalog.entries[i].state == ENTRY_LIVE)
    {
      ++live_count;
      if (app_key_equal(catalog.entries[i].app_key, begin->app_key))
        replacement = (int16_t)i;
    }
  if (replacement < 0 && live_count >= CATALOG_MAX_LIVE)
  {
    trace_clear();
    return PRISM_MGMT_ERROR_NO_SPACE;
  }
  int start = find_run(required);
  if (start < 0)
  {
    trace_clear();
    return PRISM_MGMT_ERROR_NO_SPACE;
  }
  memset(&installation, 0, sizeof(installation));
  installation.active = true;
  installation.begin = *begin;
  installation.start_block = (uint16_t)start;
  installation.crc = UINT32_MAX;
  installation.replacement_slot = replacement;
  allocation_cursor =
      (uint16_t)((installation.start_block + required) %
                 PRISM_CARTRIDGE_BLOCK_COUNT);
  trace_clear();
  return PRISM_MGMT_OK;
}

static bool ensure_install_sector_erased(uint16_t relative_sector)
{
  if (relative_sector >=
      installation.begin.required_blocks * CARTRIDGE_SECTORS_PER_BLOCK)
    return false;
  if (installation.erased[relative_sector])
    return true;
  uint32_t offset =
      block_flash_offset(installation.start_block) +
      relative_sector * FLASH_SECTOR_SIZE;
  if (!prism_flash_erase(offset, FLASH_SECTOR_SIZE))
    return false;
  installation.erased[relative_sector] = true;
  return true;
}

prism_management_status_t cartridge_storage_install_chunk(
    const prism_management_install_chunk_t *chunk, size_t payload_size)
{
  trace_set(TRACE_INSTALL_CHUNK, chunk != NULL ? chunk->offset : 0);
  /* WinUSB can report a transfer failure while an RP2040 flash operation is
   * temporarily stalling the endpoint even when the OUT packet reached us.
   * Treat an exact retry of the most recently committed range as success. */
  if (installation.active && chunk != NULL && payload_size >= 8 &&
      chunk->data_len == payload_size - 8 && chunk->data_len > 0 &&
      chunk->data_len <= 1024 &&
      chunk->offset + chunk->data_len == installation.received &&
      memcmp((const uint8_t *)XIP_BASE + PRISM_FLASH_CARTRIDGE_OFFSET +
                 installation.start_block * PRISM_CARTRIDGE_BLOCK_BYTES +
                 chunk->offset,
             chunk->data, chunk->data_len) == 0)
  {
    trace_clear();
    return PRISM_MGMT_OK;
  }
  if (!installation.active || chunk == NULL || payload_size < 8 ||
      chunk->data_len != payload_size - 8 || chunk->data_len == 0 ||
      chunk->data_len > 1024 || chunk->offset != installation.received ||
      chunk->offset + chunk->data_len > installation.begin.package_bytes ||
      (chunk->offset % FLASH_PAGE_SIZE) != 0)
  {
    trace_clear();
    return PRISM_MGMT_ERROR_BAD_MESSAGE;
  }

  uint16_t first_sector = (uint16_t)(chunk->offset / FLASH_SECTOR_SIZE);
  uint16_t last_sector =
      (uint16_t)((chunk->offset + chunk->data_len - 1u) / FLASH_SECTOR_SIZE);
  for (uint16_t sector = first_sector; sector <= last_sector; ++sector)
  {
    if (!ensure_install_sector_erased(sector))
    {
      trace_clear();
      return PRISM_MGMT_ERROR_VERIFY;
    }
  }

  uint32_t program_bytes =
      (chunk->data_len + FLASH_PAGE_SIZE - 1u) & ~(FLASH_PAGE_SIZE - 1u);
  memset(install_program_buffer, 0xff, program_bytes);
  memcpy(install_program_buffer, chunk->data, chunk->data_len);
  uint32_t flash_offset = PRISM_FLASH_CARTRIDGE_OFFSET +
                          installation.start_block *
                              PRISM_CARTRIDGE_BLOCK_BYTES +
                          chunk->offset;
  if (!prism_flash_program(flash_offset, install_program_buffer,
                           program_bytes))
  {
    trace_clear();
    return PRISM_MGMT_ERROR_VERIFY;
  }
  installation.crc =
      crc32_update(installation.crc, chunk->data, chunk->data_len);
  installation.received += chunk->data_len;
  trace_clear();
  return PRISM_MGMT_OK;
}

static prism_management_status_t finish_install(
    prism_management_status_t status)
{
  memset(&installation, 0, sizeof(installation));
  trace_clear();
  return status;
}

void cartridge_storage_install_abort(void)
{
  if (!installation.active)
    return;
  memset(&installation, 0, sizeof(installation));
  trace_clear();
}

prism_management_status_t cartridge_storage_install_commit(void)
{
  trace_set(TRACE_INSTALL_COMMIT, installation.received);
  if (!installation.active ||
      installation.received != installation.begin.package_bytes)
  {
    trace_clear();
    return PRISM_MGMT_ERROR_BAD_MESSAGE;
  }
  uint32_t final_crc = installation.crc ^ UINT32_MAX;
  if (final_crc != installation.begin.package_crc32)
    return finish_install(PRISM_MGMT_ERROR_VERIFY);
  const prism_package_header_t *header =
      (const void *)((const uint8_t *)XIP_BASE +
                     PRISM_FLASH_CARTRIDGE_OFFSET +
                     installation.start_block * PRISM_CARTRIDGE_BLOCK_BYTES);
  const prism_cartridge_t *package_descriptor = NULL;
  const char *package_id = NULL;
  if (!package_valid((const uint8_t *)header,
                     installation.begin.required_blocks *
                         PRISM_CARTRIDGE_BLOCK_BYTES) ||
      header->package_size != installation.begin.package_bytes ||
      !app_key_equal(header->app_key, installation.begin.app_key) ||
      !package_metadata((const uint8_t *)header, header,
                        &package_descriptor, &package_id, NULL))
    return finish_install(PRISM_MGMT_ERROR_INVALID_CARTRIDGE);

  const prism_registry_entry_t *id_owner = prism_registry_find(package_id);
  const prism_registry_entry_t *key_owner =
      prism_registry_find_app_key(header->app_key);
  const prism_cartridge_t *replacement =
      installation.replacement_slot >= 0
          ? load_descriptor((uint16_t)installation.replacement_slot)
          : NULL;
  if ((id_owner != NULL && id_owner->cartridge != replacement) ||
      (key_owner != NULL && key_owner->cartridge != replacement) ||
      (replacement != NULL &&
       prism_cartridge_update_check(
           catalog.entries[installation.replacement_slot].app_key,
           replacement->id, replacement->version, header->app_key,
           package_id, package_descriptor->version) !=
           PRISM_CARTRIDGE_UPDATE_MATCH))
    return finish_install(PRISM_MGMT_ERROR_INVALID_CARTRIDGE);

  if (catalog.entry_count >= CATALOG_MAX_ENTRIES)
    return finish_install(PRISM_MGMT_ERROR_NO_SPACE);
  uint16_t slot = catalog.entry_count;
  uint16_t old_count = catalog.entry_count;
  uint32_t old_generation = catalog.generation;
  catalog_entry_t replaced = {0};
  if (installation.replacement_slot >= 0)
  {
    replaced = catalog.entries[installation.replacement_slot];
    catalog.entries[installation.replacement_slot].state = ENTRY_DEAD;
  }
  catalog_entry_t *entry = &catalog.entries[slot];
  memset(entry, 0, sizeof(*entry));
  memcpy(entry->app_key, installation.begin.app_key,
         PRISM_APP_KEY_BYTES);
  entry->start_block = installation.start_block;
  entry->block_count = installation.begin.required_blocks;
  entry->package_bytes = installation.begin.package_bytes;
  entry->package_crc32 = installation.begin.package_crc32;
  entry->state = ENTRY_LIVE;
  catalog.entry_count++;
  memset(&runtime_descriptors[slot], 0, sizeof(runtime_descriptors[slot]));
  if (!catalog_save())
  {
    catalog.entry_count = old_count;
    catalog.generation = old_generation;
    memset(entry, 0, sizeof(*entry));
    if (installation.replacement_slot >= 0)
      catalog.entries[installation.replacement_slot] = replaced;
    return finish_install(PRISM_MGMT_ERROR_VERIFY);
  }
  if (installation.replacement_slot >= 0)
    memset(&runtime_descriptors[installation.replacement_slot], 0,
           sizeof(runtime_descriptors[installation.replacement_slot]));
  return finish_install(PRISM_MGMT_OK);
}

prism_management_status_t cartridge_storage_delete(
    const uint8_t app_key[PRISM_APP_KEY_BYTES])
{
  trace_set(TRACE_DELETE, 0);
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
    if (catalog.entries[i].state == ENTRY_LIVE &&
        app_key_equal(catalog.entries[i].app_key, app_key))
    {
      if (!platform_cartridge_data_delete(catalog.entries[i].app_key))
      {
        trace_clear();
        return PRISM_MGMT_ERROR_VERIFY;
      }
      catalog_entry_t previous = catalog.entries[i];
      uint32_t old_generation = catalog.generation;
      catalog.entries[i].state = ENTRY_DEAD;
      if (!catalog_save())
      {
        catalog.entries[i] = previous;
        catalog.generation = old_generation;
        trace_clear();
        return PRISM_MGMT_ERROR_VERIFY;
      }
      memset(&runtime_descriptors[i], 0, sizeof(runtime_descriptors[i]));
      trace_clear();
      return PRISM_MGMT_OK;
    }
  trace_clear();
  return PRISM_MGMT_ERROR_NOT_FOUND;
}

prism_management_status_t cartridge_storage_compact(
    cartridge_storage_progress_fn progress, void *user)
{
  trace_set(TRACE_COMPACT, 0);
  if (installation.active || move_record.stage != MOVE_NONE)
  {
    trace_clear();
    return PRISM_MGMT_ERROR_BUSY;
  }

  uint16_t live_count = 0;
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
    if (catalog.entries[i].state == ENTRY_LIVE)
      compaction_indices[live_count++] = i;
  for (uint16_t i = 0; i < live_count; ++i)
    for (uint16_t j = i + 1; j < live_count; ++j)
      if (catalog.entries[compaction_indices[j]].start_block <
          catalog.entries[compaction_indices[i]].start_block)
      {
        uint16_t swap = compaction_indices[i];
        compaction_indices[i] = compaction_indices[j];
        compaction_indices[j] = swap;
      }

  uint16_t total = 0;
  uint16_t planned_target = 0;
  for (uint16_t i = 0; i < live_count; ++i)
  {
    const catalog_entry_t *entry = &catalog.entries[compaction_indices[i]];
    if (entry->start_block != planned_target)
      total += entry->block_count;
    planned_target += entry->block_count;
  }
  uint16_t completed = 0;
  if (progress != NULL)
    progress(0, total == 0 ? 1 : total, user);

  uint16_t target = 0;
  for (uint16_t i = 0; i < live_count; ++i)
  {
    uint16_t index = compaction_indices[i];
    catalog_entry_t *entry = &catalog.entries[index];
    if (entry->start_block != target)
    {
      move_record.magic = MOVE_MAGIC;
      move_record.stage = MOVE_BEGIN;
      move_record.scratch_index =
          (uint8_t)((move_record.scratch_index + 1u) %
                    PRISM_FLASH_SCRATCH_POOL_COUNT);
      ++move_record.scratch_block_uses[move_record.scratch_index];
      move_record.entry_index = index;
      move_record.old_start = entry->start_block;
      move_record.target_start = target;
      move_record.total_blocks = entry->block_count;
      move_record.next_block = 0;
      move_record.block_crc32 = 0;
      if (!move_save() ||
          !recover_move(progress, user, &completed, total == 0 ? 1 : total))
      {
        trace_clear();
        return PRISM_MGMT_ERROR_VERIFY;
      }
    }
    target += entry->block_count;
  }

  /* Compact the catalog in place.  The previous implementation put a
   * 120-entry (3840-byte) temporary catalog on the 2048-byte core-0 stack,
   * corrupting the stack as soon as management invoked compaction. */
  uint16_t count = 0;
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
    if (catalog.entries[i].state == ENTRY_LIVE)
      catalog.entries[count++] = catalog.entries[i];
  memset(&catalog.entries[count], 0,
         (CATALOG_MAX_ENTRIES - count) * sizeof(catalog.entries[0]));
  catalog.entry_count = count;
  memset(runtime_descriptors, 0, sizeof(runtime_descriptors));
  prism_management_status_t status =
      catalog_save() ? PRISM_MGMT_OK : PRISM_MGMT_ERROR_VERIFY;
  if (status == PRISM_MGMT_OK && progress != NULL)
    progress(total == 0 ? 1 : total, total == 0 ? 1 : total, user);
  trace_clear();
  return status;
}
