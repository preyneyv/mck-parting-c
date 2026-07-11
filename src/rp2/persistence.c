#include <platform/persistence.h>

#include <stdint.h>
#include <string.h>

#include <hardware/address_mapped.h>
#include <hardware/flash.h>
#include <hardware/watchdog.h>
#include <pico/config.h>
#include <pico/flash.h>

#define SETTINGS_MAGIC 0x54455350u
#define SETTINGS_VERSION 2u
#define SETTINGS_SLOT_COUNT 2u
#define SETTINGS_PAGES_PER_SLOT (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)
#define SETTINGS_REGION_OFFSET                                                \
  (PICO_FLASH_SIZE_BYTES - SETTINGS_SLOT_COUNT * FLASH_SECTOR_SIZE)

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t generation;
  uint32_t crc32;
  uint32_t sector_erases[SETTINGS_SLOT_COUNT];
} settings_record_t;

static uint32_t current_generation;
static uint8_t current_slot;
static uint8_t current_page;
static uint32_t settings_sector_erases[SETTINGS_SLOT_COUNT];

static uint32_t crc32(const uint8_t *data, size_t size)
{
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < size; ++i)
  {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u & (uint32_t)-(int32_t)(crc & 1u));
  }
  return crc ^ UINT32_MAX;
}

static bool settings_record_valid(const uint8_t *page, size_t size,
                                  uint32_t *generation,
                                  const uint8_t **data,
                                  uint32_t erases[SETTINGS_SLOT_COUNT])
{
  const settings_record_t *record = (const void *)page;
  const uint8_t *value = page + sizeof(*record);
  if (record->magic != SETTINGS_MAGIC || record->version != SETTINGS_VERSION ||
      record->size != size ||
      sizeof(*record) + size > FLASH_PAGE_SIZE ||
      record->crc32 != crc32(value, size))
    return false;
  *generation = record->generation;
  *data = value;
  memcpy(erases, record->sector_erases, sizeof(record->sector_erases));
  return true;
}

bool platform_settings_load(void *data, size_t size)
{
  const uint8_t *flash = (const uint8_t *)XIP_BASE + SETTINGS_REGION_OFFSET;
  uint32_t best_generation = 0;
  const uint8_t *best_data = NULL;
  uint32_t best_erases[SETTINGS_SLOT_COUNT] = {0};
  for (uint8_t slot = 0; slot < SETTINGS_SLOT_COUNT; ++slot)
  {
    for (uint8_t page_index = 0; page_index < SETTINGS_PAGES_PER_SLOT;
         ++page_index)
    {
      const uint8_t *page = flash + slot * FLASH_SECTOR_SIZE +
                            page_index * FLASH_PAGE_SIZE;
      uint32_t generation;
      const uint8_t *record_data;
      uint32_t erases[SETTINGS_SLOT_COUNT];
      if (!settings_record_valid(page, size, &generation, &record_data,
                                 erases))
        continue;
      if (best_data == NULL ||
          (int32_t)(generation - best_generation) > 0)
      {
        best_generation = generation;
        best_data = record_data;
        memcpy(best_erases, erases, sizeof(best_erases));
        current_slot = slot;
        current_page = page_index;
      }
    }
  }
  if (best_data == NULL)
    return false;
  current_generation = best_generation;
  memcpy(settings_sector_erases, best_erases, sizeof(settings_sector_erases));
  memcpy(data, best_data, size);
  return true;
}

typedef struct
{
  uint32_t offset;
  const uint8_t *page;
} settings_program_context_t;

static void settings_program_callback(void *param)
{
  settings_program_context_t *context = param;
  flash_range_program(context->offset, context->page, FLASH_PAGE_SIZE);
}

static bool settings_program_page(uint32_t offset, const uint8_t *page)
{
  settings_program_context_t context = {.offset = offset, .page = page};
  bool ok = flash_safe_execute(settings_program_callback, &context, 1000) ==
            PICO_OK;
  watchdog_update();
  return ok;
}

static void settings_erase_callback(void *param)
{
  flash_range_erase(*(uint32_t *)param, FLASH_SECTOR_SIZE);
}

static bool settings_erase_sector(uint32_t offset)
{
  bool ok = flash_safe_execute(settings_erase_callback, &offset, 1000) ==
            PICO_OK;
  watchdog_update();
  return ok;
}

static bool settings_page_erased(const uint8_t *page)
{
  for (size_t i = 0; i < FLASH_PAGE_SIZE; ++i)
    if (page[i] != 0xff)
      return false;
  return true;
}

bool platform_settings_save(const void *data, size_t size)
{
  if (sizeof(settings_record_t) + size > FLASH_PAGE_SIZE)
    return false;
  const uint8_t *flash = (const uint8_t *)XIP_BASE + SETTINGS_REGION_OFFSET;
  uint8_t target_slot = current_slot;
  uint8_t target_page = UINT8_MAX;
  if (current_generation != 0)
    for (uint8_t page = (uint8_t)(current_page + 1u);
         page < SETTINGS_PAGES_PER_SLOT; ++page)
      if (settings_page_erased(flash + target_slot * FLASH_SECTOR_SIZE +
                               page * FLASH_PAGE_SIZE))
      {
        target_page = page;
        break;
      }

  uint32_t next_erases[SETTINGS_SLOT_COUNT];
  memcpy(next_erases, settings_sector_erases, sizeof(next_erases));
  if (target_page == UINT8_MAX)
  {
    target_slot = current_generation == 0
                      ? 0
                      : (uint8_t)((current_slot + 1u) % SETTINGS_SLOT_COUNT);
    uint32_t sector_offset =
        SETTINGS_REGION_OFFSET + target_slot * FLASH_SECTOR_SIZE;
    if (!settings_erase_sector(sector_offset))
      return false;
    ++next_erases[target_slot];
    target_page = 0;
  }

  uint8_t page[FLASH_PAGE_SIZE];
  memset(page, 0xff, sizeof(page));
  uint32_t next_generation = current_generation + 1u;
  settings_record_t record = {
      .magic = SETTINGS_MAGIC,
      .version = SETTINGS_VERSION,
      .size = (uint16_t)size,
      .generation = next_generation,
      .crc32 = crc32(data, size),
  };
  memcpy(record.sector_erases, next_erases, sizeof(next_erases));
  memcpy(page, &record, sizeof(record));
  memcpy(page + sizeof(record), data, size);
  uint32_t offset = SETTINGS_REGION_OFFSET +
                    target_slot * FLASH_SECTOR_SIZE +
                    target_page * FLASH_PAGE_SIZE;
  if (!settings_program_page(offset, page))
    return false;
  current_generation = next_generation;
  current_slot = target_slot;
  current_page = target_page;
  memcpy(settings_sector_erases, next_erases, sizeof(settings_sector_erases));
  return true;
}

/* Cartridge save data is an append-only log in one of two arenas. During
 * compaction the other arena is erased and populated first; its separate
 * commit page is written last, so boot always has one complete arena. */
#define DATA_ARENA_MAGIC 0x32524150u /* PAR2 */
#define DATA_COMMIT_MAGIC 0x32434150u /* PAC2 */
#define DATA_RECORD_MAGIC 0x32444150u /* PAD2 */
#define DATA_TOMBSTONE 1u
#define DATA_REGION_OFFSET (15u * 1024u * 1024u)
#define DATA_REGION_END SETTINGS_REGION_OFFSET
#define DATA_ARENA_BYTES ((DATA_REGION_END - DATA_REGION_OFFSET) / 2u)
#define DATA_RECORDS_OFFSET (2u * FLASH_PAGE_SIZE)
#define DATA_MAX_APPS 64u

_Static_assert((DATA_REGION_END - DATA_REGION_OFFSET) % (2u * FLASH_SECTOR_SIZE) == 0,
               "persistence arenas must contain whole sectors");

typedef struct
{
  uint32_t magic;
  uint32_t generation;
  uint32_t crc32;
} data_arena_marker_t;

typedef struct
{
  uint32_t magic;
  uint32_t app_id;
  uint16_t schema;
  uint16_t flags;
  uint32_t size;
  uint32_t generation;
  uint32_t crc32;
} data_record_t;

typedef struct
{
  uint32_t app_id;
  const data_record_t *record;
  uint32_t offset;
  uint32_t bytes;
} latest_record_t;

typedef struct
{
  uint32_t offset;
  const uint8_t *page;
} page_context_t;

static bool data_initialized;
static uint8_t data_active_arena;
static uint32_t data_arena_generation;
static uint32_t data_record_generation;
static uint32_t data_append_offset;
static bool data_tail_tainted;
/* Core 0 has a 2 KiB stack; reuse one scan workspace instead of placing a
 * 1 KiB latest-record table in several persistence call frames. */
static latest_record_t latest_workspace[DATA_MAX_APPS];

static uint32_t arena_offset(uint8_t arena)
{
  return DATA_REGION_OFFSET + arena * DATA_ARENA_BYTES;
}

static uint32_t marker_crc(uint32_t generation)
{
  return crc32((const uint8_t *)&generation, sizeof(generation));
}

static bool page_erased(const uint8_t *page)
{
  for (size_t i = 0; i < FLASH_PAGE_SIZE; ++i)
    if (page[i] != 0xff)
      return false;
  return true;
}

static bool sector_erased(const uint8_t *sector)
{
  for (size_t i = 0; i < FLASH_SECTOR_SIZE; ++i)
    if (sector[i] != 0xff)
      return false;
  return true;
}

static void program_page_callback(void *param)
{
  page_context_t *context = param;
  flash_range_program(context->offset, context->page, FLASH_PAGE_SIZE);
}

static bool program_page(uint32_t offset, const uint8_t page[FLASH_PAGE_SIZE])
{
  page_context_t context = {.offset = offset, .page = page};
  return flash_safe_execute(program_page_callback, &context, 1000) == PICO_OK;
}

static void erase_sector_callback(void *param)
{
  flash_range_erase(*(uint32_t *)param, FLASH_SECTOR_SIZE);
}

static bool erase_sector(uint32_t offset)
{
  bool ok = flash_safe_execute(erase_sector_callback, &offset, 1000) == PICO_OK;
  watchdog_update();
  return ok;
}

static bool erase_arena(uint8_t arena)
{
  uint32_t base = arena_offset(arena);
  const uint8_t *xip = (const uint8_t *)XIP_BASE + base;
  for (uint32_t offset = 0; offset < DATA_ARENA_BYTES;
       offset += FLASH_SECTOR_SIZE)
    if (!sector_erased(xip + offset) && !erase_sector(base + offset))
      return false;
  return true;
}

static bool arena_marker_valid(uint8_t arena)
{
  const uint8_t *xip = (const uint8_t *)XIP_BASE + arena_offset(arena);
  const data_arena_marker_t *start = (const void *)xip;
  const data_arena_marker_t *commit = (const void *)(xip + FLASH_PAGE_SIZE);
  return start->magic == DATA_ARENA_MAGIC &&
         commit->magic == DATA_COMMIT_MAGIC &&
         start->generation == commit->generation &&
         start->crc32 == marker_crc(start->generation) &&
         commit->crc32 == marker_crc(commit->generation);
}

static bool write_marker(uint8_t arena, bool commit, uint32_t generation)
{
  uint8_t page[FLASH_PAGE_SIZE];
  memset(page, 0xff, sizeof(page));
  data_arena_marker_t marker = {
      .magic = commit ? DATA_COMMIT_MAGIC : DATA_ARENA_MAGIC,
      .generation = generation,
      .crc32 = marker_crc(generation),
  };
  memcpy(page, &marker, sizeof(marker));
  return program_page(arena_offset(arena) +
                          (commit ? FLASH_PAGE_SIZE : 0u),
                      page);
}

static uint32_t record_bytes(const data_record_t *record)
{
  uint32_t raw = (uint32_t)sizeof(*record) + record->size;
  return (raw + FLASH_PAGE_SIZE - 1u) & ~(FLASH_PAGE_SIZE - 1u);
}

static bool record_valid(const data_record_t *record, uint32_t remaining)
{
  if (record->magic != DATA_RECORD_MAGIC || record->flags > DATA_TOMBSTONE ||
      record->size > remaining - sizeof(*record) ||
      record_bytes(record) > remaining)
    return false;
  if (record->flags == DATA_TOMBSTONE)
    return record->size == 0 && record->crc32 == 0;
  return record->crc32 ==
         crc32((const uint8_t *)record + sizeof(*record), record->size);
}

static size_t scan_latest(uint8_t arena, latest_record_t *latest,
                          size_t capacity, bool *tainted,
                          uint32_t *append_offset)
{
  const uint8_t *xip = (const uint8_t *)XIP_BASE + arena_offset(arena);
  uint32_t offset = DATA_RECORDS_OFFSET;
  size_t count = 0;
  *tainted = false;
  while (offset + FLASH_PAGE_SIZE <= DATA_ARENA_BYTES)
  {
    const uint8_t *page = xip + offset;
    if (page_erased(page))
      break;
    const data_record_t *record = (const void *)page;
    if (record->magic != DATA_RECORD_MAGIC ||
        record->size > DATA_ARENA_BYTES - offset - sizeof(*record))
    {
      *tainted = true;
      break;
    }
    uint32_t bytes = record_bytes(record);
    if (bytes == 0 || bytes > DATA_ARENA_BYTES - offset)
    {
      *tainted = true;
      break;
    }
    if (record_valid(record, DATA_ARENA_BYTES - offset))
    {
      if (record->generation > data_record_generation)
        data_record_generation = record->generation;
      size_t index = 0;
      while (index < count && latest[index].app_id != record->app_id)
        ++index;
      if (index == count && count < capacity)
        ++count;
      if (index < capacity &&
          (latest[index].record == NULL ||
           (int32_t)(record->generation - latest[index].record->generation) > 0))
        latest[index] = (latest_record_t){record->app_id, record, offset, bytes};
    }
    offset += bytes;
  }
  *append_offset = offset;
  return count;
}

static size_t scan_latest_workspace(uint8_t arena, bool *tainted,
                                    uint32_t *append_offset)
{
  memset(latest_workspace, 0, sizeof(latest_workspace));
  return scan_latest(arena, latest_workspace, DATA_MAX_APPS, tainted,
                     append_offset);
}

static bool initialize_data_store(void)
{
  if (data_initialized)
    return true;
  bool valid0 = arena_marker_valid(0);
  bool valid1 = arena_marker_valid(1);
  if (!valid0 && !valid1)
  {
    if (!erase_arena(0) || !write_marker(0, false, 1) ||
        !write_marker(0, true, 1))
      return false;
    data_active_arena = 0;
    data_arena_generation = 1;
  }
  else
  {
    const data_arena_marker_t *marker0 =
        (const void *)((const uint8_t *)XIP_BASE + arena_offset(0));
    const data_arena_marker_t *marker1 =
        (const void *)((const uint8_t *)XIP_BASE + arena_offset(1));
    data_active_arena = valid1 &&
                                (!valid0 || (int32_t)(marker1->generation -
                                                      marker0->generation) > 0)
                            ? 1
                            : 0;
    const data_arena_marker_t *active =
        data_active_arena ? marker1 : marker0;
    data_arena_generation = active->generation;
  }
  scan_latest_workspace(data_active_arena, &data_tail_tainted,
                        &data_append_offset);
  data_initialized = true;
  return true;
}

static bool append_record(uint32_t app_id, uint16_t schema, uint16_t flags,
                          const void *data, size_t size)
{
  data_record_t record = {
      .magic = DATA_RECORD_MAGIC,
      .app_id = app_id,
      .schema = schema,
      .flags = flags,
      .size = (uint32_t)size,
      .generation = ++data_record_generation,
      .crc32 = flags == DATA_TOMBSTONE ? 0 : crc32(data, size),
  };
  uint32_t bytes = record_bytes(&record);
  if (data_tail_tainted || data_append_offset + bytes > DATA_ARENA_BYTES)
    return false;

  uint8_t page[FLASH_PAGE_SIZE];
  for (uint32_t page_offset = 0; page_offset < bytes;
       page_offset += FLASH_PAGE_SIZE)
  {
    memset(page, 0xff, sizeof(page));
    for (uint32_t i = 0; i < FLASH_PAGE_SIZE; ++i)
    {
      uint32_t source = page_offset + i;
      if (source < sizeof(record))
        page[i] = ((const uint8_t *)&record)[source];
      else if (source - sizeof(record) < size)
        page[i] = ((const uint8_t *)data)[source - sizeof(record)];
    }
    if (!program_page(arena_offset(data_active_arena) + data_append_offset +
                          page_offset,
                      page))
      return false;
    watchdog_update();
  }
  data_append_offset += bytes;
  return true;
}

static bool compact_data_store(void)
{
  bool tainted;
  uint32_t ignored;
  size_t count = scan_latest_workspace(data_active_arena, &tainted, &ignored);
  uint8_t target = data_active_arena ^ 1u;
  uint32_t generation = data_arena_generation + 1u;
  if (!erase_arena(target) || !write_marker(target, false, generation))
    return false;

  uint32_t destination = DATA_RECORDS_OFFSET;
  const uint8_t *source_base =
      (const uint8_t *)XIP_BASE + arena_offset(data_active_arena);
  uint8_t page[FLASH_PAGE_SIZE];
  for (size_t i = 0; i < count; ++i)
  {
    if (latest_workspace[i].record == NULL ||
        latest_workspace[i].record->flags == DATA_TOMBSTONE)
      continue;
    if (destination + latest_workspace[i].bytes > DATA_ARENA_BYTES)
      return false;
    for (uint32_t offset = 0; offset < latest_workspace[i].bytes;
         offset += FLASH_PAGE_SIZE)
    {
      memcpy(page, source_base + latest_workspace[i].offset + offset,
             sizeof(page));
      if (!program_page(arena_offset(target) + destination + offset, page))
        return false;
      watchdog_update();
    }
    destination += latest_workspace[i].bytes;
  }
  if (!write_marker(target, true, generation))
    return false;
  data_active_arena = target;
  data_arena_generation = generation;
  data_append_offset = destination;
  data_tail_tainted = false;
  return true;
}

bool platform_cartridge_data_load(uint32_t app_id, uint16_t schema,
                                  void *data, size_t size)
{
  if (!initialize_data_store())
    return false;
  bool tainted;
  uint32_t append;
  size_t count = scan_latest_workspace(data_active_arena, &tainted, &append);
  for (size_t i = 0; i < count; ++i)
    if (latest_workspace[i].app_id == app_id &&
        latest_workspace[i].record != NULL &&
        latest_workspace[i].record->flags != DATA_TOMBSTONE &&
        latest_workspace[i].record->schema == schema &&
        latest_workspace[i].record->size == size)
    {
      memcpy(data, (const uint8_t *)latest_workspace[i].record +
                       sizeof(data_record_t), size);
      return true;
    }
  return false;
}

static bool cartridge_data_matches(uint32_t app_id, uint16_t schema,
                                   const void *data, size_t size)
{
  bool tainted;
  uint32_t append;
  size_t count = scan_latest_workspace(data_active_arena, &tainted, &append);
  for (size_t i = 0; i < count; ++i)
  {
    const data_record_t *record = latest_workspace[i].record;
    if (latest_workspace[i].app_id == app_id && record != NULL &&
        record->flags != DATA_TOMBSTONE && record->schema == schema &&
        record->size == size)
      return memcmp((const uint8_t *)record + sizeof(*record), data, size) == 0;
  }
  return false;
}

bool platform_cartridge_data_save(uint32_t app_id, uint16_t schema,
                                  const void *data, size_t size)
{
  if (size > UINT32_MAX || !initialize_data_store())
    return false;
  if (cartridge_data_matches(app_id, schema, data, size))
    return true;
  if (append_record(app_id, schema, 0, data, size))
    return true;
  return compact_data_store() && append_record(app_id, schema, 0, data, size);
}

bool platform_cartridge_data_delete(uint32_t app_id)
{
  if (!initialize_data_store())
    return false;
  if (append_record(app_id, 0, DATA_TOMBSTONE, NULL, 0))
    return true;
  return compact_data_store() &&
         append_record(app_id, 0, DATA_TOMBSTONE, NULL, 0);
}
