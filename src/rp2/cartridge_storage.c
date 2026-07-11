#include "cartridge_storage.h"

#include <stdbool.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <hardware/address_mapped.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <hardware/structs/watchdog.h>
#include <pico/flash.h>

#include <platform/persistence.h>
#include <prism/package.h>
#include <prism/registry.h>
#include <qrcodegen.h>
#include <shared/audio/song.h>
#include <u8g2.h>

#define CARTRIDGE_REGION_OFFSET (2u * 1024u * 1024u)
#define CARTRIDGE_BLOCK_COUNT PRISM_CARTRIDGE_BLOCK_COUNT
#define CARTRIDGE_SECTORS_PER_BLOCK                                      \
  (PRISM_CARTRIDGE_BLOCK_BYTES / FLASH_SECTOR_SIZE)
#define CARTRIDGE_SECTOR_COUNT                                           \
  (CARTRIDGE_BLOCK_COUNT * CARTRIDGE_SECTORS_PER_BLOCK)
#define CARTRIDGE_SCRATCH_OFFSET (14u * 1024u * 1024u)
#define CATALOG_SLOT0_OFFSET (CARTRIDGE_SCRATCH_OFFSET + PRISM_CARTRIDGE_BLOCK_BYTES)
#define CATALOG_SLOT1_OFFSET (CATALOG_SLOT0_OFFSET + FLASH_SECTOR_SIZE)
#define MOVE_SLOT0_OFFSET (CATALOG_SLOT1_OFFSET + FLASH_SECTOR_SIZE)
#define MOVE_JOURNAL_SECTOR_COUNT 8u
#define MOVE_JOURNAL_PAGES_PER_SECTOR (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)
#define MOVE_JOURNAL_PAGE_COUNT                                          \
  (MOVE_JOURNAL_SECTOR_COUNT * MOVE_JOURNAL_PAGES_PER_SECTOR)
#define CARTRIDGE_EXTRA_SCRATCH_OFFSET                                  \
  (CARTRIDGE_SCRATCH_OFFSET + 2u * PRISM_CARTRIDGE_BLOCK_BYTES)
#define CARTRIDGE_SCRATCH_POOL_COUNT PRISM_FLASH_SCRATCH_POOL_MAX
#define CARTRIDGE_AUX_REGION_END (15u * 1024u * 1024u)
#define CATALOG_MAGIC 0x54414350u /* PCAT */
#define CATALOG_VERSION 3u
#define CATALOG_FLAG_DEFAULTS_SEEDED (1u << 0)
#define CATALOG_MAX_ENTRIES 120u
#define CATALOG_MAX_LIVE 32u
#define ENTRY_LIVE 1u
#define ENTRY_DEAD 2u
#define MOVE_MAGIC 0x324f4d50u /* PMO2 */
#define MOVE_NONE 0u
#define MOVE_BEGIN 1u
#define MOVE_SCRATCH_READY 2u
#define MOVE_DEST_READY 3u
#define TRACE_INSTALL_BEGIN 0x49424547u /* IBEG */
#define TRACE_INSTALL_CHUNK 0x4943484bu /* ICHK */
#define TRACE_INSTALL_COMMIT 0x49434f4du /* ICOM */
#define TRACE_DELETE 0x44454c45u /* DELE */
#define TRACE_COMPACT 0x434f4d50u /* COMP */
#define TRACE_ERASE 0x45524153u /* ERAS */
#define TRACE_PROGRAM 0x50524f47u /* PROG */

typedef struct __attribute__((packed))
{
  uint8_t uuid[16];
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
  uint32_t journal_sector_erases[MOVE_JOURNAL_SECTOR_COUNT];
  uint32_t scratch_block_uses[CARTRIDGE_SCRATCH_POOL_COUNT];
  uint32_t crc32;
} move_record_t;

_Static_assert(sizeof(move_record_t) <= FLASH_PAGE_SIZE,
               "move journal records must fit one flash page");
_Static_assert(CARTRIDGE_EXTRA_SCRATCH_OFFSET +
                       (CARTRIDGE_SCRATCH_POOL_COUNT - 1u) *
                           PRISM_CARTRIDGE_BLOCK_BYTES <=
                   CARTRIDGE_AUX_REGION_END,
               "scratch pool must not overlap cartridge persistence");

typedef struct
{
  uint32_t offset;
  uint32_t size;
  const uint8_t *data;
} flash_operation_t;

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
  watchdog_hw->scratch[0] = stage;
  watchdog_hw->scratch[1] = detail;
}

static void trace_clear(void)
{
  watchdog_hw->scratch[0] = 0;
  watchdog_hw->scratch[1] = 0;
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
  return (const uint8_t *)XIP_BASE + CARTRIDGE_REGION_OFFSET +
         entry->start_block * PRISM_CARTRIDGE_BLOCK_BYTES;
}

static uintptr_t resolve_import(uint16_t symbol);
extern float __aeabi_fdiv(float numerator, float denominator);

static bool package_valid(const uint8_t *package, uint32_t allocated_bytes)
{
  if (package == NULL || allocated_bytes < sizeof(prism_package_header_t))
    return false;
  const prism_package_header_t *header = (const void *)package;
  if (header->magic != PRISM_PACKAGE_MAGIC ||
      header->format_version != PRISM_PACKAGE_FORMAT_VERSION ||
      header->header_size != sizeof(*header) ||
      header->cartridge_abi != PRISM_CARTRIDGE_ABI_V1 ||
      header->package_size < sizeof(*header) ||
      header->package_size > allocated_bytes ||
      header->u8g2_abi_hash != PRISM_U8G2_ABI_HASH ||
      header->relocation_count > PRISM_PACKAGE_MAX_RELOCATIONS ||
      header->import_count > PRISM_PACKAGE_MAX_IMPORTS ||
      memchr(header->slug, '\0', sizeof(header->slug)) == NULL ||
      memchr(header->name, '\0', sizeof(header->name)) == NULL ||
      header->slug[0] == '\0' || header->name[0] == '\0')
    return false;

  if (!range_valid(header->image_offset, header->image_size,
                   header->package_size) ||
      !range_valid(header->descriptor_offset, sizeof(prism_cartridge_t),
                   header->package_size) ||
      !range_valid(header->got_offset, header->got_size,
                   header->package_size) ||
      header->got_base_offset > header->got_size ||
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

static uintptr_t resolve_import(uint16_t symbol)
{
  switch ((prism_package_import_symbol_t)symbol)
  {
  case PRISM_IMPORT_U8G2_SET_DRAW_COLOR: return IMPORT_ADDRESS(u8g2_SetDrawColor);
  case PRISM_IMPORT_U8G2_SET_FONT: return IMPORT_ADDRESS(u8g2_SetFont);
  case PRISM_IMPORT_U8G2_DRAW_STR: return IMPORT_ADDRESS(u8g2_DrawStr);
  case PRISM_IMPORT_SNPRINTF: return IMPORT_ADDRESS(snprintf);
  case PRISM_IMPORT_U8G2_GET_STR_WIDTH: return IMPORT_ADDRESS(u8g2_GetStrWidth);
  case PRISM_IMPORT_FONT_6X10_TF: return (uintptr_t)u8g2_font_6x10_tf;
  case PRISM_IMPORT_U8G2_DRAW_XBM: return IMPORT_ADDRESS(u8g2_DrawXBM);
  case PRISM_IMPORT_U8G2_DRAW_BOX: return IMPORT_ADDRESS(u8g2_DrawBox);
  case PRISM_IMPORT_U8G2_DRAW_FRAME: return IMPORT_ADDRESS(u8g2_DrawFrame);
  case PRISM_IMPORT_U8G2_DRAW_RBOX: return IMPORT_ADDRESS(u8g2_DrawRBox);
  case PRISM_IMPORT_U8G2_DRAW_RFRAME: return IMPORT_ADDRESS(u8g2_DrawRFrame);
  case PRISM_IMPORT_U8G2_DRAW_HLINE: return IMPORT_ADDRESS(u8g2_DrawHLine);
  case PRISM_IMPORT_U8G2_DRAW_VLINE: return IMPORT_ADDRESS(u8g2_DrawVLine);
  case PRISM_IMPORT_U8G2_DRAW_PIXEL: return IMPORT_ADDRESS(u8g2_DrawPixel);
  case PRISM_IMPORT_U8G2_DRAW_LINE: return IMPORT_ADDRESS(u8g2_DrawLine);
  case PRISM_IMPORT_U8G2_DRAW_CIRCLE: return IMPORT_ADDRESS(u8g2_DrawCircle);
  case PRISM_IMPORT_U8G2_DRAW_DISC: return IMPORT_ADDRESS(u8g2_DrawDisc);
  case PRISM_IMPORT_U8G2_DRAW_ELLIPSE: return IMPORT_ADDRESS(u8g2_DrawEllipse);
  case PRISM_IMPORT_U8G2_DRAW_FILLED_ELLIPSE: return IMPORT_ADDRESS(u8g2_DrawFilledEllipse);
  case PRISM_IMPORT_U8G2_DRAW_TRIANGLE: return IMPORT_ADDRESS(u8g2_DrawTriangle);
  case PRISM_IMPORT_U8G2_DRAW_ARC: return IMPORT_ADDRESS(u8g2_DrawArc);
  case PRISM_IMPORT_U8G2_DRAW_UTF8: return IMPORT_ADDRESS(u8g2_DrawUTF8);
  case PRISM_IMPORT_U8G2_SET_BITMAP_MODE: return IMPORT_ADDRESS(u8g2_SetBitmapMode);
  case PRISM_IMPORT_FONT_4X6_TF: return (uintptr_t)u8g2_font_4x6_tf;
  case PRISM_IMPORT_FONT_5X7_MR: return (uintptr_t)u8g2_font_5x7_mr;
  case PRISM_IMPORT_FONT_5X7_TF: return (uintptr_t)u8g2_font_5x7_tf;
  case PRISM_IMPORT_FONT_5X7_TR: return (uintptr_t)u8g2_font_5x7_tr;
  case PRISM_IMPORT_FONT_7X14_MR: return (uintptr_t)u8g2_font_7x14_mr;
  case PRISM_IMPORT_FONT_7X14B_MR: return (uintptr_t)u8g2_font_7x14B_mr;
  case PRISM_IMPORT_FONT_U8GLIB_4_TF: return (uintptr_t)u8g2_font_u8glib_4_tf;
  case PRISM_IMPORT_MEMCPY: return IMPORT_ADDRESS(memcpy);
  case PRISM_IMPORT_MEMSET: return IMPORT_ADDRESS(memset);
  case PRISM_IMPORT_MEMMOVE: return IMPORT_ADDRESS(memmove);
  case PRISM_IMPORT_MEMCMP: return IMPORT_ADDRESS(memcmp);
  case PRISM_IMPORT_STRLEN: return IMPORT_ADDRESS(strlen);
  case PRISM_IMPORT_STRCMP: return IMPORT_ADDRESS(strcmp);
  case PRISM_IMPORT_STRNCPY: return IMPORT_ADDRESS(strncpy);
  case PRISM_IMPORT_MALLOC: return IMPORT_ADDRESS(malloc);
  case PRISM_IMPORT_CALLOC: return IMPORT_ADDRESS(calloc);
  case PRISM_IMPORT_REALLOC: return IMPORT_ADDRESS(realloc);
  case PRISM_IMPORT_FREE: return IMPORT_ADDRESS(free);
  case PRISM_IMPORT_SINF: return IMPORT_ADDRESS(sinf);
  case PRISM_IMPORT_COSF: return IMPORT_ADDRESS(cosf);
  case PRISM_IMPORT_SQRTF: return IMPORT_ADDRESS(sqrtf);
  case PRISM_IMPORT_FMODF: return IMPORT_ADDRESS(fmodf);
  case PRISM_IMPORT_RAND: return IMPORT_ADDRESS(rand);
  case PRISM_IMPORT_SRAND: return IMPORT_ADDRESS(srand);
  case PRISM_IMPORT_QRCODE_GET_SIZE: return IMPORT_ADDRESS(qrcodegen_getSize);
  case PRISM_IMPORT_QRCODE_GET_MODULE: return IMPORT_ADDRESS(qrcodegen_getModule);
  case PRISM_IMPORT_FLOORF: return IMPORT_ADDRESS(floorf);
  case PRISM_IMPORT_QSORT: return IMPORT_ADDRESS(qsort);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_INIT: return IMPORT_ADDRESS(audio_song_player_init);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_SET_HOOK: return IMPORT_ADDRESS(audio_song_player_set_hook);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_PLAY: return IMPORT_ADDRESS(audio_song_player_play);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_STOP: return IMPORT_ADDRESS(audio_song_player_stop);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_PAUSE: return IMPORT_ADDRESS(audio_song_player_pause);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_RESUME: return IMPORT_ADDRESS(audio_song_player_resume);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_TICK: return IMPORT_ADDRESS(audio_song_player_tick);
  case PRISM_IMPORT_AEABI_FDIV: return IMPORT_ADDRESS(__aeabi_fdiv);
  case PRISM_IMPORT_STRCAT: return IMPORT_ADDRESS(strcat);
  case PRISM_IMPORT_STRNCMP: return IMPORT_ADDRESS(strncmp);
  case PRISM_IMPORT_AUDIO_SYNTH_PATCH_CONFIG_SET: return IMPORT_ADDRESS(audio_synth_patch_config_set);
  case PRISM_IMPORT_AUDIO_SYNTH_ENQUEUE: return IMPORT_ADDRESS(audio_synth_enqueue);
  case PRISM_IMPORT_AUDIO_SONG_PLAYER_SEEK: return IMPORT_ADDRESS(audio_song_player_seek);
  case PRISM_IMPORT_EXPF: return IMPORT_ADDRESS(expf);
  case PRISM_IMPORT_ABS: return IMPORT_ADDRESS(abs);
  case PRISM_IMPORT_FABSF: return IMPORT_ADDRESS(fabsf);
  case PRISM_IMPORT_ATAN2F: return IMPORT_ADDRESS(atan2f);
  case PRISM_IMPORT_FMAXF: return IMPORT_ADDRESS(fmaxf);
  default: return 0;
  }
}

static uint32_t catalog_offset(uint8_t slot)
{
  return slot ? CATALOG_SLOT1_OFFSET : CATALOG_SLOT0_OFFSET;
}

static bool catalog_valid(const catalog_t *value)
{
  return value->magic == CATALOG_MAGIC && value->version == CATALOG_VERSION &&
         value->entry_count <= CATALOG_MAX_ENTRIES &&
         value->entries_crc32 ==
             crc32(value->entries,
                   value->entry_count * sizeof(catalog_entry_t));
}

static bool catalog_contains_uuid(const uint8_t uuid[16])
{
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
    if (catalog.entries[i].state == ENTRY_LIVE &&
        memcmp(catalog.entries[i].uuid, uuid, 16) == 0)
      return true;
  return false;
}

static bool seed_default_cartridge(const default_cartridge_t *value)
{
  size_t size = (size_t)(value->end - value->start);
  if (size < sizeof(prism_package_header_t) || size > UINT32_MAX)
    return false;
  const prism_package_header_t *header = (const void *)value->start;
  if (header->magic != PRISM_PACKAGE_MAGIC || header->package_size != size)
    return false;
  if (catalog_contains_uuid(header->uuid))
    return true;

  prism_management_install_begin_t begin = {0};
  memcpy(begin.uuid, header->uuid, sizeof(begin.uuid));
  begin.package_bytes = (uint32_t)size;
  begin.package_crc32 = crc32(value->start, size);
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
  if ((catalog.flags & CATALOG_FLAG_DEFAULTS_SEEDED) != 0)
    return;
  for (size_t i = 0;
       i < sizeof(default_cartridges) / sizeof(default_cartridges[0]); ++i)
    if (!seed_default_cartridge(&default_cartridges[i]))
      return;
  catalog.flags |= CATALOG_FLAG_DEFAULTS_SEEDED;
  catalog_save();
}

static void flash_erase_callback(void *param)
{
  flash_operation_t *operation = param;
  flash_range_erase(operation->offset, operation->size);
}

static void flash_program_callback(void *param)
{
  flash_operation_t *operation = param;
  flash_range_program(operation->offset, operation->data, operation->size);
}

static bool erase_range(uint32_t offset, uint32_t size)
{
  uint32_t parent_stage = watchdog_hw->scratch[0];
  uint32_t parent_detail = watchdog_hw->scratch[1];
  trace_set(TRACE_ERASE, offset);
  flash_operation_t operation = {.offset = offset, .size = size};
  bool ok = flash_safe_execute(flash_erase_callback, &operation, 1000) == PICO_OK;
  watchdog_update();
  trace_set(parent_stage, parent_detail);
  return ok;
}

static bool program_range(uint32_t offset, const uint8_t *data, uint32_t size)
{
  uint32_t parent_stage = watchdog_hw->scratch[0];
  uint32_t parent_detail = watchdog_hw->scratch[1];
  trace_set(TRACE_PROGRAM, offset);
  flash_operation_t operation = {.offset = offset, .size = size, .data = data};
  bool ok = flash_safe_execute(flash_program_callback, &operation, 1000) == PICO_OK;
  watchdog_update();
  trace_set(parent_stage, parent_detail);
  return ok;
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
  if (!erase_range(offset, FLASH_SECTOR_SIZE) ||
      !program_range(offset, catalog_sector, FLASH_SECTOR_SIZE))
    return false;
  catalog_slot = next_slot;
  return true;
}

static uint32_t move_sector_offset(uint8_t sector)
{
  return MOVE_SLOT0_OFFSET + sector * FLASH_SECTOR_SIZE;
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
         record->scratch_index < CARTRIDGE_SCRATCH_POOL_COUNT &&
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
                                    MOVE_JOURNAL_SECTOR_COUNT)
                        : 0;
    if (!erase_range(move_sector_offset(target_sector), FLASH_SECTOR_SIZE))
      return false;
    ++move_record.journal_sector_erases[target_sector];
    target_page = 0;
  }

  move_record.crc32 = move_crc(&move_record);
  memset(move_page, 0xff, sizeof(move_page));
  memcpy(move_page, &move_record, sizeof(move_record));
  if (!program_range(move_page_offset(target_sector, target_page), move_page,
                     sizeof(move_page)))
    return false;
  move_journal_sector = target_sector;
  move_journal_page = target_page;
  move_journal_location_valid = true;
  return true;
}

static uint32_t scratch_offset(uint8_t index)
{
  if (index == 0)
    return CARTRIDGE_SCRATCH_OFFSET;
  return CARTRIDGE_EXTRA_SCRATCH_OFFSET +
         (index - 1u) * PRISM_CARTRIDGE_BLOCK_BYTES;
}

static uint32_t block_flash_offset(uint16_t block)
{
  return CARTRIDGE_REGION_OFFSET + block * PRISM_CARTRIDGE_BLOCK_BYTES;
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
    if (!erase_range(flash_offset + sector, FLASH_SECTOR_SIZE))
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
    if (!program_range(destination_offset + offset, page, sizeof(page)))
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
                      CARTRIDGE_SCRATCH_POOL_COUNT);
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
      (const void *)((const uint8_t *)XIP_BASE + CATALOG_SLOT0_OFFSET);
  const catalog_t *slot1 =
      (const void *)((const uint8_t *)XIP_BASE + CATALOG_SLOT1_OFFSET);
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
  for (uint8_t sector = 0; sector < MOVE_JOURNAL_SECTOR_COUNT; ++sector)
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
  allocation_cursor = (uint16_t)(catalog.generation % CARTRIDGE_BLOCK_COUNT);
  recover_move(NULL, NULL, NULL, 0);
  seed_default_cartridges();
  reconcile_catalog();
}

static void block_map(uint8_t map[CARTRIDGE_BLOCK_COUNT])
{
  memset(map, 0, CARTRIDGE_BLOCK_COUNT);
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
  {
    const catalog_entry_t *entry = &catalog.entries[i];
    if (entry->state != ENTRY_LIVE)
      continue;
    for (uint16_t block = entry->start_block;
         block < entry->start_block + entry->block_count &&
         block < CARTRIDGE_BLOCK_COUNT;
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
         block < CARTRIDGE_BLOCK_COUNT;
         ++block)
      if (map[block] == 0)
        map[block] = ENTRY_DEAD;
  }
}

void cartridge_storage_info(prism_management_storage_info_t *info)
{
  memset(info, 0, sizeof(*info));
  block_map(info->block_states);
  info->total_blocks = CARTRIDGE_BLOCK_COUNT;
  info->scratch_blocks = CARTRIDGE_SCRATCH_POOL_COUNT;
  info->required_blocks = installation.active ? installation.begin.required_blocks : 0;
  uint16_t free_run = 0, reclaimable_run = 0;
  for (uint16_t block = 0; block < CARTRIDGE_BLOCK_COUNT; ++block)
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

static bool pointer_in_image(const void *pointer, const uint8_t *image,
                             uint32_t image_size, bool thumb)
{
  if (pointer == NULL)
    return true;
  uintptr_t value = (uintptr_t)pointer;
  if (thumb)
    value &= ~1u;
  return value >= (uintptr_t)image &&
         value < (uintptr_t)image + image_size;
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
  if (entry->block_count == 0 || entry->start_block >= CARTRIDGE_BLOCK_COUNT ||
      entry->block_count > CARTRIDGE_BLOCK_COUNT - entry->start_block)
    return NULL;
  const uint8_t *package = entry_package(entry);
  uint32_t allocated = entry->block_count * PRISM_CARTRIDGE_BLOCK_BYTES;
  if (!package_valid(package, allocated))
    return NULL;
  const prism_package_header_t *header = (const void *)package;
  if (header->package_size != entry->package_bytes ||
      memcmp(header->uuid, entry->uuid, sizeof(entry->uuid)) != 0)
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
      descriptor->abi_version != PRISM_CARTRIDGE_ABI_V1 ||
      descriptor->descriptor_size != sizeof(*descriptor) ||
      descriptor->app_id != header->app_id ||
      descriptor->persistent_size != header->persistent_size ||
      descriptor->persistent_schema_version != header->persistent_schema ||
      !pointer_in_image(descriptor->slug, image, header->image_size, false) ||
      !pointer_in_image(descriptor->name, image, header->image_size, false) ||
      !bytes_in_image(descriptor->icon, PRISM_CARTRIDGE_ICON_BYTES, image,
                      header->image_size) ||
      !pointer_in_image((const void *)descriptor->enter, image,
                        header->image_size, true) ||
      !pointer_in_image((const void *)descriptor->tick, image,
                        header->image_size, true) ||
      !pointer_in_image((const void *)descriptor->frame, image,
                        header->image_size, true) ||
      !pointer_in_image((const void *)descriptor->pause, image,
                        header->image_size, true) ||
      !pointer_in_image((const void *)descriptor->resume, image,
                        header->image_size, true) ||
      !pointer_in_image((const void *)descriptor->leave, image,
                        header->image_size, true) ||
      strcmp(descriptor->slug, header->slug) != 0 ||
      strcmp(descriptor->name, header->name) != 0)
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

const prism_cartridge_t *cartridge_storage_find_uuid(const uint8_t uuid[16])
{
  if (uuid == NULL)
    return NULL;
  for (uint16_t slot = 0; slot < catalog.entry_count; ++slot)
    if (catalog.entries[slot].state == ENTRY_LIVE &&
        memcmp(catalog.entries[slot].uuid, uuid, 16) == 0)
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
    return false;
  uint8_t *got = allocation;
  uint8_t *rw = allocation + header->got_size;
  memcpy(got, package + header->got_offset, header->got_size);
  if (header->rw_size > 0)
  {
    memcpy(rw, package + header->rw_offset, header->rw_init_size);
    memset(rw + header->rw_init_size, 0,
           header->rw_size - header->rw_init_size);
  }

  const prism_package_relocation_t *relocations =
      (const void *)(package + header->relocations_offset);
  uint32_t got_end = header->got_offset + header->got_size;
  uint32_t rw_init_end = header->rw_offset + header->rw_init_size;
  for (uint32_t i = 0; i < header->relocation_count; ++i)
  {
    uint32_t patch = relocations[i].patch_offset;
    uint32_t *word;
    if (patch >= header->got_offset && patch < got_end)
      word = (void *)(got + patch - header->got_offset);
    else if (patch >= header->rw_offset && patch < rw_init_end)
      word = (void *)(rw + patch - header->rw_offset);
    else
      continue;
    uint32_t linked = *word & ~1u;
    uint32_t thumb = *word & 1u;
    if (linked >= header->image_size)
    {
      uint32_t rw_relative = linked - header->image_size;
      if (rw_relative >= header->rw_size)
      {
        free(allocation);
        return false;
      }
      *word = (uint32_t)(uintptr_t)(rw + rw_relative) | thumb;
    }
    else
      *word += (uint32_t)(uintptr_t)image;
  }

  const prism_package_import_t *imports =
      (const void *)(package + header->imports_offset);
  for (uint32_t i = 0; i < header->import_count; ++i)
  {
    uintptr_t address = resolve_import(imports[i].symbol);
    if (address == 0)
    {
      free(allocation);
      return false;
    }
    uint32_t *word =
        (void *)(got + imports[i].patch_offset - header->got_offset);
    *word = (uint32_t)address;
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
                             prism_management_cartridge_entry_t *entry)
{
  const catalog_entry_t *stored = live_entry(index);
  if (stored == NULL)
    return false;
  const prism_package_header_t *header =
      (const void *)((const uint8_t *)XIP_BASE + CARTRIDGE_REGION_OFFSET +
                     stored->start_block * PRISM_CARTRIDGE_BLOCK_BYTES);
  if (header->magic != PRISM_PACKAGE_MAGIC)
    return false;
  memset(entry, 0, sizeof(*entry));
  memcpy(entry->uuid, stored->uuid, sizeof(entry->uuid));
  entry->package_bytes = stored->package_bytes;
  entry->persistent_bytes = header->persistent_size;
  entry->blocks = stored->block_count;
  memcpy(entry->slug, header->slug, sizeof(entry->slug));
  memcpy(entry->name, header->name, sizeof(entry->name));
  entry->slug[sizeof(entry->slug) - 1] = '\0';
  entry->name[sizeof(entry->name) - 1] = '\0';
  return true;
}

static bool uuid_equal(const uint8_t a[16], const uint8_t b[16])
{
  return memcmp(a, b, 16) == 0;
}

static int find_run(uint16_t required)
{
  uint8_t map[CARTRIDGE_BLOCK_COUNT];
  block_map(map);
  for (uint16_t distance = 0; distance < CARTRIDGE_BLOCK_COUNT; ++distance)
  {
    uint16_t start =
        (uint16_t)((allocation_cursor + distance) % CARTRIDGE_BLOCK_COUNT);
    if (start + required > CARTRIDGE_BLOCK_COUNT)
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
  if (required == 0 || required > CARTRIDGE_BLOCK_COUNT ||
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
      if (uuid_equal(catalog.entries[i].uuid, begin->uuid))
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
                 CARTRIDGE_BLOCK_COUNT);
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
  if (!erase_range(offset, FLASH_SECTOR_SIZE))
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
      memcmp((const uint8_t *)XIP_BASE + CARTRIDGE_REGION_OFFSET +
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

  uint32_t position = 0;
  uint8_t page[FLASH_PAGE_SIZE];
  while (position < chunk->data_len)
  {
    uint32_t absolute = chunk->offset + position;
    uint16_t relative_sector = (uint16_t)(absolute / FLASH_SECTOR_SIZE);
    if (!ensure_install_sector_erased(relative_sector))
    {
      trace_clear();
      return PRISM_MGMT_ERROR_VERIFY;
    }
    uint32_t length = chunk->data_len - position;
    if (length > FLASH_PAGE_SIZE)
      length = FLASH_PAGE_SIZE;
    memset(page, 0xff, sizeof(page));
    memcpy(page, chunk->data + position, length);
    uint32_t flash_offset = CARTRIDGE_REGION_OFFSET +
                            installation.start_block *
                                PRISM_CARTRIDGE_BLOCK_BYTES +
                            absolute;
    if (!program_range(flash_offset, page, FLASH_PAGE_SIZE))
    {
      trace_clear();
      return PRISM_MGMT_ERROR_VERIFY;
    }
    position += length;
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
      (const void *)((const uint8_t *)XIP_BASE + CARTRIDGE_REGION_OFFSET +
                     installation.start_block * PRISM_CARTRIDGE_BLOCK_BYTES);
  if (!package_valid((const uint8_t *)header,
                     installation.begin.required_blocks *
                         PRISM_CARTRIDGE_BLOCK_BYTES) ||
      header->package_size != installation.begin.package_bytes ||
      !uuid_equal(header->uuid, installation.begin.uuid) ||
      header->slug[0] == '\0' || header->name[0] == '\0')
    return finish_install(PRISM_MGMT_ERROR_INVALID_CARTRIDGE);

  const prism_registry_entry_t *slug_owner = prism_registry_find(header->slug);
  const prism_registry_entry_t *id_owner = prism_registry_find_app_id(header->app_id);
  const prism_cartridge_t *replacement = installation.replacement_slot >= 0
      ? load_descriptor((uint16_t)installation.replacement_slot)
      : NULL;
  if ((slug_owner != NULL && slug_owner->cartridge != replacement) ||
      (id_owner != NULL && id_owner->cartridge != replacement) ||
      (replacement != NULL &&
       (replacement->app_id != header->app_id ||
        strcmp(replacement->slug, header->slug) != 0)))
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
  memcpy(entry->uuid, installation.begin.uuid, 16);
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

prism_management_status_t cartridge_storage_delete(const uint8_t uuid[16])
{
  trace_set(TRACE_DELETE, 0);
  for (uint16_t i = 0; i < catalog.entry_count; ++i)
    if (catalog.entries[i].state == ENTRY_LIVE &&
        uuid_equal(catalog.entries[i].uuid, uuid))
    {
      const prism_package_header_t *header =
          (const void *)((const uint8_t *)XIP_BASE + CARTRIDGE_REGION_OFFSET +
                         catalog.entries[i].start_block *
                             PRISM_CARTRIDGE_BLOCK_BYTES);
      if (!platform_cartridge_data_delete(header->app_id))
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
                    CARTRIDGE_SCRATCH_POOL_COUNT);
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
