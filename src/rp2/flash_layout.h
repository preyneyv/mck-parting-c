#pragma once

#include <hardware/flash.h>
#include <pico/config.h>

#include <prism/management_protocol.h>

#define PRISM_FLASH_BYTES (16u * 1024u * 1024u)

#define PRISM_FLASH_FIRMWARE_OFFSET 0u
#define PRISM_FLASH_FIRMWARE_BYTES (2u * 1024u * 1024u)
#define PRISM_FLASH_FIRMWARE_END                                           \
  (PRISM_FLASH_FIRMWARE_OFFSET + PRISM_FLASH_FIRMWARE_BYTES)

#define PRISM_FLASH_CARTRIDGE_OFFSET PRISM_FLASH_FIRMWARE_END
#define PRISM_FLASH_CARTRIDGE_BYTES                                        \
  (PRISM_STORAGE_BLOCK_COUNT * PRISM_STORAGE_BLOCK_BYTES)
#define PRISM_FLASH_CARTRIDGE_END                                          \
  (PRISM_FLASH_CARTRIDGE_OFFSET + PRISM_FLASH_CARTRIDGE_BYTES)

#define PRISM_FLASH_COMPACTION_SCRATCH_OFFSET PRISM_FLASH_CARTRIDGE_END
#define PRISM_FLASH_CATALOG0_OFFSET                                        \
  (PRISM_FLASH_COMPACTION_SCRATCH_OFFSET + PRISM_STORAGE_BLOCK_BYTES)
#define PRISM_FLASH_CATALOG_SLOT_BYTES (2u * FLASH_SECTOR_SIZE)
#define PRISM_FLASH_CATALOG1_OFFSET                                        \
  (PRISM_FLASH_CATALOG0_OFFSET + PRISM_FLASH_CATALOG_SLOT_BYTES)
#define PRISM_FLASH_MOVE_JOURNAL_OFFSET                                    \
  (PRISM_FLASH_CATALOG1_OFFSET + PRISM_FLASH_CATALOG_SLOT_BYTES)
#define PRISM_FLASH_MOVE_JOURNAL_SECTORS 8u
#define PRISM_FLASH_MOVE_JOURNAL_BYTES                                     \
  (PRISM_FLASH_MOVE_JOURNAL_SECTORS * FLASH_SECTOR_SIZE)
#define PRISM_FLASH_MOVE_JOURNAL_END                                       \
  (PRISM_FLASH_MOVE_JOURNAL_OFFSET + PRISM_FLASH_MOVE_JOURNAL_BYTES)

#define PRISM_FLASH_EXTRA_SCRATCH_OFFSET                                   \
  (PRISM_FLASH_COMPACTION_SCRATCH_OFFSET +                                \
   2u * PRISM_STORAGE_BLOCK_BYTES)
#define PRISM_FLASH_SCRATCH_POOL_COUNT PRISM_FLASH_SCRATCH_POOL_MAX
#define PRISM_FLASH_EXTRA_SCRATCH_COUNT                                    \
  (PRISM_FLASH_SCRATCH_POOL_COUNT - 1u)
#define PRISM_FLASH_EXTRA_SCRATCH_END                                      \
  (PRISM_FLASH_EXTRA_SCRATCH_OFFSET +                                     \
   PRISM_FLASH_EXTRA_SCRATCH_COUNT * PRISM_STORAGE_BLOCK_BYTES)

#define PRISM_FLASH_CARTRIDGE_DATA_OFFSET (15u * 1024u * 1024u)
#define PRISM_FLASH_SETTINGS_SLOT_COUNT 2u
#define PRISM_FLASH_SETTINGS_BYTES                                         \
  (PRISM_FLASH_SETTINGS_SLOT_COUNT * FLASH_SECTOR_SIZE)
#define PRISM_FLASH_SETTINGS_OFFSET                                        \
  (PRISM_FLASH_BYTES - PRISM_FLASH_SETTINGS_BYTES)
#define PRISM_FLASH_CARTRIDGE_DATA_END PRISM_FLASH_SETTINGS_OFFSET
#define PRISM_FLASH_CARTRIDGE_DATA_BYTES                                   \
  (PRISM_FLASH_CARTRIDGE_DATA_END - PRISM_FLASH_CARTRIDGE_DATA_OFFSET)
#define PRISM_FLASH_CARTRIDGE_DATA_ARENA_BYTES                             \
  (PRISM_FLASH_CARTRIDGE_DATA_BYTES / 2u)
#define PRISM_FLASH_CARTRIDGE_DATA_ARENA0_OFFSET                           \
  PRISM_FLASH_CARTRIDGE_DATA_OFFSET
#define PRISM_FLASH_CARTRIDGE_DATA_ARENA1_OFFSET                           \
  (PRISM_FLASH_CARTRIDGE_DATA_ARENA0_OFFSET +                             \
   PRISM_FLASH_CARTRIDGE_DATA_ARENA_BYTES)
#define PRISM_FLASH_SETTINGS_END                                           \
  (PRISM_FLASH_SETTINGS_OFFSET + PRISM_FLASH_SETTINGS_BYTES)

_Static_assert(PICO_FLASH_SIZE_BYTES == PRISM_FLASH_BYTES,
               "Prism's flash map requires a 16 MiB flash chip");
_Static_assert(PRISM_FLASH_CARTRIDGE_END == 14u * 1024u * 1024u,
               "cartridge blocks must end at the auxiliary region");
_Static_assert(PRISM_FLASH_FIRMWARE_END <= PRISM_FLASH_CARTRIDGE_OFFSET,
               "firmware overlaps cartridge storage");
_Static_assert(PRISM_FLASH_COMPACTION_SCRATCH_OFFSET +
                       PRISM_STORAGE_BLOCK_BYTES <=
                   PRISM_FLASH_CATALOG0_OFFSET,
               "compaction scratch overlaps the catalog");
_Static_assert(PRISM_FLASH_CATALOG0_OFFSET +
                       PRISM_FLASH_CATALOG_SLOT_BYTES <=
                   PRISM_FLASH_CATALOG1_OFFSET,
               "catalog slots overlap");
_Static_assert(PRISM_FLASH_CATALOG1_OFFSET +
                       PRISM_FLASH_CATALOG_SLOT_BYTES <=
                   PRISM_FLASH_MOVE_JOURNAL_OFFSET,
               "catalog overlaps the move journal");
_Static_assert(PRISM_FLASH_MOVE_JOURNAL_END <=
                   PRISM_FLASH_EXTRA_SCRATCH_OFFSET,
               "cartridge metadata overlaps compaction scratch");
_Static_assert(PRISM_FLASH_EXTRA_SCRATCH_END ==
                   PRISM_FLASH_CARTRIDGE_DATA_OFFSET,
               "compaction scratch must end at cartridge persistence");
_Static_assert(PRISM_FLASH_CARTRIDGE_DATA_END ==
                   PRISM_FLASH_SETTINGS_OFFSET,
               "cartridge persistence overlaps settings");
_Static_assert(PRISM_FLASH_CARTRIDGE_DATA_ARENA1_OFFSET +
                       PRISM_FLASH_CARTRIDGE_DATA_ARENA_BYTES ==
                   PRISM_FLASH_CARTRIDGE_DATA_END,
               "cartridge persistence arenas do not fill their region");
_Static_assert(PRISM_FLASH_SETTINGS_END == PRISM_FLASH_BYTES,
               "settings must end at the flash boundary");
_Static_assert(PRISM_FLASH_CARTRIDGE_DATA_BYTES %
                       (2u * FLASH_SECTOR_SIZE) ==
                   0,
               "persistence arenas must contain whole sectors");

_Static_assert(PRISM_FLASH_FIRMWARE_END % FLASH_SECTOR_SIZE == 0,
               "firmware boundary must be sector aligned");
_Static_assert(PRISM_FLASH_CARTRIDGE_OFFSET % FLASH_SECTOR_SIZE == 0,
               "cartridge storage must be sector aligned");
_Static_assert(PRISM_STORAGE_BLOCK_BYTES % FLASH_SECTOR_SIZE == 0,
               "cartridge blocks must contain whole sectors");
_Static_assert(PRISM_FLASH_COMPACTION_SCRATCH_OFFSET % FLASH_SECTOR_SIZE == 0,
               "compaction scratch must be sector aligned");
_Static_assert(PRISM_FLASH_CATALOG0_OFFSET % FLASH_SECTOR_SIZE == 0 &&
                   PRISM_FLASH_CATALOG1_OFFSET % FLASH_SECTOR_SIZE == 0,
               "catalog slots must be sector aligned");
_Static_assert(PRISM_FLASH_MOVE_JOURNAL_OFFSET % FLASH_SECTOR_SIZE == 0,
               "move journal must be sector aligned");
_Static_assert(PRISM_FLASH_EXTRA_SCRATCH_OFFSET % FLASH_SECTOR_SIZE == 0,
               "extra scratch must be sector aligned");
_Static_assert(PRISM_FLASH_CARTRIDGE_DATA_OFFSET % FLASH_SECTOR_SIZE == 0 &&
                   PRISM_FLASH_SETTINGS_OFFSET % FLASH_SECTOR_SIZE == 0,
               "persistence regions must be sector aligned");
