#pragma once

#include <stdint.h>

#include <prism/cartridge_identity.h>
#include <prism/types.h>

#define PRISM_MANAGEMENT_MAGIC 0x4d535250u /* "PRSM", little endian */
#define PRISM_MANAGEMENT_VERSION 1u
#define PRISM_MANAGEMENT_MAX_PAYLOAD 4096u
#define PRISM_SCREEN_BYTES 1024u
#define PRISM_SERIAL_BYTES 8u
#define PRISM_CARTRIDGE_BLOCK_BYTES (128u * 1024u)
#define PRISM_CARTRIDGE_BLOCK_COUNT 96u
#define PRISM_LED_PALETTE_MAX 16u
#define PRISM_CARTRIDGE_NAME_MAX 31u

#if defined(__GNUC__)
#define PRISM_PACKED __attribute__((packed))
#else
#define PRISM_PACKED
#endif

typedef enum
{
  PRISM_MGMT_HELLO = 0x01,
  PRISM_MGMT_DEVICE_INFO = 0x02,
  PRISM_MGMT_STORAGE_INFO = 0x03,
  PRISM_MGMT_CARTRIDGE_LIST = 0x04,
  PRISM_MGMT_CARTRIDGE_ICON = 0x05,

  PRISM_MGMT_INSTALL_BEGIN = 0x10,
  PRISM_MGMT_INSTALL_CHUNK = 0x11,
  PRISM_MGMT_INSTALL_COMMIT = 0x12,
  PRISM_MGMT_CARTRIDGE_DELETE = 0x13,
  PRISM_MGMT_COMPACT = 0x14,
  PRISM_MGMT_OPERATION_PROGRESS = 0x15,
  PRISM_MGMT_CARTRIDGE_LAUNCH = 0x16,

  PRISM_MGMT_SETTINGS_GET = 0x20,
  PRISM_MGMT_SETTINGS_PREVIEW = 0x21,

  PRISM_MGMT_MIRROR_SUBSCRIBE = 0x30,
  PRISM_MGMT_MIRROR_UNSUBSCRIBE = 0x31,
  PRISM_MGMT_MIRROR_FRAME = 0x32,
  PRISM_MGMT_REMOTE_INPUT = 0x33,
  PRISM_MGMT_HEARTBEAT = 0x34,
  PRISM_MGMT_LOG = 0x35,
} prism_management_message_type_t;

typedef enum
{
  PRISM_MGMT_FLAG_RESPONSE = 1u << 0,
  PRISM_MGMT_FLAG_EVENT = 1u << 1,
  PRISM_MGMT_FLAG_ERROR = 1u << 2,
} prism_management_flags_t;

typedef enum
{
  PRISM_MGMT_OK = 0,
  PRISM_MGMT_ERROR_BAD_MESSAGE = 1,
  PRISM_MGMT_ERROR_UNSUPPORTED = 2,
  PRISM_MGMT_ERROR_BUSY = 3,
  PRISM_MGMT_ERROR_NO_SPACE = 4,
  PRISM_MGMT_ERROR_NOT_FOUND = 5,
  PRISM_MGMT_ERROR_VERIFY = 6,
  PRISM_MGMT_ERROR_INVALID_CARTRIDGE = 7,
} prism_management_status_t;

typedef struct PRISM_PACKED
{
  uint32_t magic;
  uint8_t version;
  uint8_t type;
  uint16_t flags;
  uint32_t request_id;
  uint32_t payload_len;
} prism_management_header_t;

typedef struct PRISM_PACKED
{
  uint16_t status;
  uint16_t detail;
} prism_management_result_t;

enum
{
  PRISM_DEVICE_STATE_SLEEPING = 1u << 0,
};

typedef struct PRISM_PACKED
{
  uint8_t serial[PRISM_SERIAL_BYTES];
  uint16_t protocol_version;
  uint16_t firmware_major;
  uint16_t firmware_minor;
  uint16_t firmware_patch;
  uint32_t flash_bytes;
  uint32_t cartridge_block_bytes;
  uint32_t capabilities;
} prism_management_device_info_t;

typedef struct PRISM_PACKED
{
  uint16_t total_blocks;
  uint16_t live_blocks;
  uint16_t erased_blocks;
  uint16_t dead_blocks;
  uint16_t largest_free_run;
  uint16_t largest_reclaimable_run;
  uint16_t scratch_blocks;
  uint16_t required_blocks;
  uint8_t block_states[PRISM_CARTRIDGE_BLOCK_COUNT];
} prism_management_storage_info_t;

enum
{
  PRISM_CAP_MIRROR = 1u << 0,
  PRISM_CAP_REMOTE_INPUT = 1u << 1,
  PRISM_CAP_LOGS = 1u << 2,
  PRISM_CAP_SETTINGS = 1u << 3,
  PRISM_CAP_CARTRIDGES = 1u << 4,
  PRISM_CAP_COMPACTION = 1u << 5,
  PRISM_CAP_APP_LAUNCH = 1u << 6,
};
#define PRISM_FLASH_SCRATCH_POOL_MAX 7u

typedef struct PRISM_PACKED
{
  uint16_t total_count;
  uint16_t start_index;
  uint16_t count;
  uint16_t string_bytes;
} prism_management_cartridge_list_t;

typedef struct PRISM_PACKED
{
  uint8_t app_key[PRISM_APP_KEY_BYTES];
  uint32_t package_bytes;
  uint32_t persistent_bytes;
  uint32_t version;
  uint16_t blocks;
  uint16_t policy;
  uint16_t id_offset;
  uint16_t id_length;
  uint16_t name_offset;
  uint16_t name_length;
} prism_management_cartridge_entry_t;

typedef struct PRISM_PACKED
{
  uint16_t start_index;
  uint16_t reserved;
} prism_management_cartridge_list_request_t;

typedef enum
{
  PRISM_LED_STATIC = 0,
  PRISM_LED_BREATHING = 1,
  PRISM_LED_CROSSFADE = 2,
  PRISM_LED_RAINBOW = 3,
} prism_led_effect_t;

typedef struct PRISM_PACKED
{
  uint8_t effect;
  uint8_t palette_len;
  uint16_t speed_ms;
  uint8_t phase_offset;
  uint8_t reserved[3];
  uint8_t colors[PRISM_LED_PALETTE_MAX][3];
} prism_led_settings_t;

typedef struct PRISM_PACKED
{
  uint8_t volume;
  uint8_t linked_leds;
  uint8_t brightness;
  uint8_t settings_revision;
  prism_led_settings_t leds[2];
} prism_management_settings_t;

enum
{
  PRISM_REMOTE_LEFT = 1u << 0,
  PRISM_REMOTE_RIGHT = 1u << 1,
  PRISM_REMOTE_MENU = 1u << 2,
};

typedef struct PRISM_PACKED
{
  uint32_t sequence;
  uint8_t framebuffer[PRISM_SCREEN_BYTES];
  uint8_t led_rgb[2][3];
  uint8_t buttons;
  uint8_t reserved;
} prism_management_mirror_frame_t;

typedef struct PRISM_PACKED
{
  uint8_t buttons;
  uint8_t reserved[3];
} prism_management_remote_input_t;

typedef struct PRISM_PACKED
{
  uint8_t app_key[PRISM_APP_KEY_BYTES];
  uint32_t package_bytes;
  uint32_t package_crc32;
  uint16_t required_blocks;
  uint16_t reserved;
  char name[PRISM_CARTRIDGE_NAME_MAX + 1];
  uint8_t icon[PRISM_CARTRIDGE_ICON_BYTES];
} prism_management_install_begin_t;

typedef struct PRISM_PACKED
{
  uint32_t offset;
  uint16_t data_len;
  uint16_t reserved;
  uint8_t data[];
} prism_management_install_chunk_t;

typedef struct PRISM_PACKED
{
  uint8_t app_key[PRISM_APP_KEY_BYTES];
} prism_management_cartridge_id_t;

enum
{
  PRISM_OPERATION_COMPACT = 1,
};

enum
{
  PRISM_OPERATION_PHASE_RUNNING = 1,
  PRISM_OPERATION_PHASE_COMPLETE = 2,
};

typedef struct PRISM_PACKED
{
  uint8_t operation;
  uint8_t phase;
  uint16_t completed_blocks;
  uint16_t total_blocks;
  uint16_t reserved;
} prism_management_progress_t;

_Static_assert(sizeof(prism_management_header_t) == 16,
               "management header is part of the wire format");
_Static_assert(sizeof(prism_management_cartridge_list_t) == 8,
               "cartridge lists are part of the wire format");
_Static_assert(sizeof(prism_management_cartridge_entry_t) == 40,
               "cartridge entries are part of the wire format");
_Static_assert(sizeof(prism_led_settings_t) == 56,
               "LED settings are part of the wire format");
_Static_assert(sizeof(prism_management_settings_t) == 116,
               "settings are part of the wire format");
_Static_assert(sizeof(prism_management_mirror_frame_t) == 1036,
               "mirror frames are consumed directly by the browser");
