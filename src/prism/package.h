#pragma once

#include <stdint.h>

#include <prism/cartridge.h>
#include <prism/management_protocol.h>

#define PRISM_PACKAGE_MAGIC 0x4b505250u /* PRPK */
#define PRISM_PACKAGE_FORMAT_VERSION 3u
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
  PRISM_IMPORT_U8G2_SET_DRAW_COLOR = 1,
  PRISM_IMPORT_U8G2_SET_FONT,
  PRISM_IMPORT_U8G2_DRAW_STR,
  PRISM_IMPORT_SNPRINTF,
  PRISM_IMPORT_U8G2_GET_STR_WIDTH,
  PRISM_IMPORT_FONT_6X10_TF,
  PRISM_IMPORT_U8G2_DRAW_XBM,
  PRISM_IMPORT_U8G2_DRAW_BOX,
  PRISM_IMPORT_U8G2_DRAW_FRAME,
  PRISM_IMPORT_U8G2_DRAW_RBOX,
  PRISM_IMPORT_U8G2_DRAW_RFRAME,
  PRISM_IMPORT_U8G2_DRAW_HLINE,
  PRISM_IMPORT_U8G2_DRAW_VLINE,
  PRISM_IMPORT_U8G2_DRAW_PIXEL,
  PRISM_IMPORT_U8G2_DRAW_LINE,
  PRISM_IMPORT_U8G2_DRAW_CIRCLE,
  PRISM_IMPORT_U8G2_DRAW_DISC,
  PRISM_IMPORT_U8G2_DRAW_ELLIPSE,
  PRISM_IMPORT_U8G2_DRAW_FILLED_ELLIPSE,
  PRISM_IMPORT_U8G2_DRAW_TRIANGLE,
  PRISM_IMPORT_U8G2_DRAW_ARC,
  PRISM_IMPORT_U8G2_DRAW_UTF8,
  PRISM_IMPORT_U8G2_SET_BITMAP_MODE,
  PRISM_IMPORT_FONT_4X6_TF,
  PRISM_IMPORT_FONT_5X7_MR,
  PRISM_IMPORT_FONT_5X7_TF,
  PRISM_IMPORT_FONT_5X7_TR,
  PRISM_IMPORT_FONT_7X14_MR,
  PRISM_IMPORT_FONT_7X14B_MR,
  PRISM_IMPORT_FONT_U8GLIB_4_TF,
  PRISM_IMPORT_MEMCPY,
  PRISM_IMPORT_MEMSET,
  PRISM_IMPORT_MEMMOVE,
  PRISM_IMPORT_MEMCMP,
  PRISM_IMPORT_STRLEN,
  PRISM_IMPORT_STRCMP,
  PRISM_IMPORT_STRNCPY,
  PRISM_IMPORT_MALLOC,
  PRISM_IMPORT_CALLOC,
  PRISM_IMPORT_REALLOC,
  PRISM_IMPORT_FREE,
  PRISM_IMPORT_SINF,
  PRISM_IMPORT_COSF,
  PRISM_IMPORT_SQRTF,
  PRISM_IMPORT_FMODF,
  PRISM_IMPORT_RAND,
  PRISM_IMPORT_SRAND,
  PRISM_IMPORT_QRCODE_GET_SIZE,
  PRISM_IMPORT_QRCODE_GET_MODULE,
  PRISM_IMPORT_FLOORF,
  PRISM_IMPORT_QSORT,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_INIT,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_SET_HOOK,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_PLAY,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_STOP,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_PAUSE,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_RESUME,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_TICK,
  PRISM_IMPORT_AEABI_FDIV,
  PRISM_IMPORT_STRCAT,
  PRISM_IMPORT_STRNCMP,
  PRISM_IMPORT_AUDIO_SYNTH_PATCH_CONFIG_SET,
  PRISM_IMPORT_AUDIO_SYNTH_ENQUEUE,
  PRISM_IMPORT_AUDIO_SONG_PLAYER_SEEK,
  PRISM_IMPORT_EXPF,
  PRISM_IMPORT_ABS,
  PRISM_IMPORT_FABSF,
  PRISM_IMPORT_ATAN2F,
  PRISM_IMPORT_FMAXF,
} prism_package_import_symbol_t;

typedef struct __attribute__((packed))
{
  uint32_t magic;
  uint16_t format_version;
  uint16_t header_size;
  uint16_t cartridge_abi;
  uint16_t flags;
  uint32_t package_size;
  uint8_t uuid[PRISM_CARTRIDGE_UUID_BYTES];
  uint32_t app_id;
  uint32_t persistent_size;
  uint16_t persistent_schema;
  uint16_t reserved0;
  char slug[PRISM_CARTRIDGE_SLUG_MAX + 1];
  char name[PRISM_CARTRIDGE_NAME_MAX + 1];
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
  uint8_t reserved[92];
} prism_package_header_t;

_Static_assert(sizeof(prism_package_header_t) == PRISM_PACKAGE_HEADER_BYTES,
               "package header is part of the file format");
