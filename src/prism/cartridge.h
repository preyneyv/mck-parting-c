#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <u8g2.h>

#include <prism/types.h>
#include <prism/asset_pack.h>
#include <shared/audio/synth.h>

#define PRISM_CARTRIDGE_MAGIC 0x50524354u /* PRCT */
#define PRISM_API_ABI_V1 1u
#define PRISM_API_ABI_VERSION PRISM_API_ABI_V1
#define PRISM_API_V1_FUNCTION_COUNT 27u
#define PRISM_API_V1_STRUCT_SIZE 116u
#define PRISM_CARTRIDGE_ABI_V1 1u
#define PRISM_CARTRIDGE_ABI_VERSION PRISM_CARTRIDGE_ABI_V1

typedef uint8_t prism_button_t;
enum
{
  PRISM_BUTTON_NONE = 0,
  PRISM_BUTTON_LEFT = 1,
  PRISM_BUTTON_RIGHT = 2,
  PRISM_BUTTON_MENU = 3,
};

typedef uint8_t prism_ui_sound_t;
enum
{
  PRISM_UI_SOUND_NAVIGATE = 0,
  PRISM_UI_SOUND_HOLD_TICK = 1,
};

typedef struct prism_api_v1
{
  uint32_t abi_version;
  uint32_t struct_size;

  u8g2_t *(*display)(void);
  uint32_t (*ticks)(void);
  prism_time_t (*now_us)(void);
  int64_t (*time_diff_us)(prism_time_t from, prism_time_t to);

  bool (*button_pressed)(prism_button_t button);
  bool (*button_edge)(prism_button_t button);
  float (*button_hold_ratio)(prism_button_t button);
  void (*buttons_reset)(void);

  void (*led_set)(uint8_t led, prism_color_t color);
  audio_synth_t *(*synth)(void);
  prism_power_state_t (*power_state)(void);
  void (*sleep)(void);
  void (*system_reset)(void);
  void (*persist)(void);
  void (*keep_awake)(void);

  int (*anim_to)(volatile int32_t *subject, int32_t target,
                 uint32_t duration, prism_anim_ease_t easing,
                 prism_anim_done_fn callback, void *user);
  void (*anim_cancel)(volatile int32_t *subject, int finish);

  bool (*leaderboard_qrcode)(uint8_t app_id, const void *data, size_t data_len,
                             uint8_t *qrcode);

  bool (*button_keydown)(prism_button_t button);
  bool (*button_keyup)(prism_button_t button);
  uint32_t (*button_keydown_tick)(prism_button_t button);
  uint32_t (*button_keyup_tick)(prism_button_t button);
  void (*ui_sound)(prism_ui_sound_t sound, uint8_t value);
  uint32_t (*asset_pack_count)(void);
  bool (*asset_pack_get)(uint32_t index, prism_asset_pack_info_t *info);
  bool (*asset_file_get)(prism_asset_pack_handle_t pack, uint32_t index,
                         prism_asset_file_t *file);
  void (*close)(void);
} prism_api_v1_t;

typedef struct prism_context
{
  const prism_api_v1_t *api;
  const struct prism_cartridge *cartridge;
  void *persistent;
  size_t persistent_size;
} prism_t;

typedef void (*prism_lifecycle_fn)(prism_t *prism);

typedef struct prism_cartridge
{
  uint32_t magic;
  uint16_t abi_version;
  uint16_t descriptor_size;
  /* Call tick once per this many 960 Hz engine ticks. Zero means the default
   * divider of one and is normalized by the package loader. */
  uint32_t tick_divider;
  uint32_t version;
  const char *id;
  const char *name;
  const uint8_t *icon;

  prism_lifecycle_fn enter;
  prism_lifecycle_fn tick;
  prism_lifecycle_fn frame;
  prism_lifecycle_fn pause;
  prism_lifecycle_fn resume;
  prism_lifecycle_fn leave;
  size_t persistent_size;
  uint16_t persistent_schema_version;
  uint16_t reserved;
} prism_cartridge_t;

_Static_assert(sizeof(prism_button_t) == 1,
               "prism_button_t is part of the ABI");

#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(prism_api_v1_t) == PRISM_API_V1_STRUCT_SIZE,
               "prism_api_v1_t layout changed");
_Static_assert(sizeof(prism_t) == 16, "prism_t layout changed");
_Static_assert(sizeof(prism_cartridge_t) == 60,
               "prism_cartridge_t layout changed");
_Static_assert(offsetof(prism_cartridge_t, version) == 12,
               "prism_cartridge_t layout changed");
_Static_assert(offsetof(prism_cartridge_t, id) == 16,
               "prism_cartridge_t layout changed");
_Static_assert(offsetof(prism_cartridge_t, enter) == 28,
               "prism_cartridge_t layout changed");
_Static_assert(offsetof(prism_cartridge_t, persistent_size) == 52,
               "prism_cartridge_t layout changed");
_Static_assert(offsetof(prism_cartridge_t, persistent_schema_version) == 56,
               "prism_cartridge_t layout changed");
#endif

#if defined(__GNUC__)
#define PRISM_CARTRIDGE_EXPORT                                                \
  __attribute__((used, aligned(4), section(".prism_cartridge")))
#else
#define PRISM_CARTRIDGE_EXPORT
#endif

#define PRISM_CARTRIDGE(_symbol, ...)                                         \
  PRISM_CARTRIDGE_EXPORT const prism_cartridge_t _symbol = {                  \
      .magic = PRISM_CARTRIDGE_MAGIC,                                         \
      .abi_version = PRISM_CARTRIDGE_ABI_VERSION,                             \
      .descriptor_size = sizeof(prism_cartridge_t),                           \
      __VA_ARGS__                                                              \
  }

static inline u8g2_t *prism_display(prism_t *prism)
{
  return prism->api->display();
}

static inline uint32_t prism_ticks(prism_t *prism)
{
  return prism->api->ticks();
}

static inline uint32_t prism_millis(prism_t *prism)
{
  return prism_ms_from_ticks(prism_ticks(prism));
}

static inline prism_time_t prism_now_us(prism_t *prism)
{
  return prism->api->now_us();
}

static inline int64_t prism_time_diff_us(prism_t *prism, prism_time_t from,
                                         prism_time_t to)
{
  return prism->api->time_diff_us(from, to);
}

static inline bool prism_button_pressed(prism_t *prism, prism_button_t button)
{
  return prism->api->button_pressed(button);
}

static inline bool prism_button_edge(prism_t *prism, prism_button_t button)
{
  return prism->api->button_edge(button);
}

static inline bool prism_button_keydown(prism_t *prism, prism_button_t button)
{
  return prism->api->button_keydown(button);
}

static inline bool prism_button_keyup(prism_t *prism, prism_button_t button)
{
  return prism->api->button_keyup(button);
}

static inline uint32_t prism_button_keydown_tick(prism_t *prism,
                                                  prism_button_t button)
{
  return prism->api->button_keydown_tick(button);
}

static inline uint32_t prism_button_keyup_tick(prism_t *prism,
                                                prism_button_t button)
{
  return prism->api->button_keyup_tick(button);
}

static inline void prism_ui_sound(prism_t *prism, prism_ui_sound_t sound,
                                  uint8_t value)
{
  prism->api->ui_sound(sound, value);
}

static inline float prism_button_hold_ratio(prism_t *prism,
                                             prism_button_t button)
{
  return prism->api->button_hold_ratio(button);
}

static inline prism_button_t prism_button_first_pressed(prism_t *prism)
{
  if (prism_button_pressed(prism, PRISM_BUTTON_LEFT))
    return PRISM_BUTTON_LEFT;
  if (prism_button_pressed(prism, PRISM_BUTTON_RIGHT))
    return PRISM_BUTTON_RIGHT;
  if (prism_button_pressed(prism, PRISM_BUTTON_MENU))
    return PRISM_BUTTON_MENU;
  return PRISM_BUTTON_NONE;
}

static inline void prism_buttons_reset(prism_t *prism)
{
  prism->api->buttons_reset();
}

static inline void prism_led_set(prism_t *prism, uint8_t led,
                                 prism_color_t color)
{
  prism->api->led_set(led, color);
}

static inline audio_synth_t *prism_synth(prism_t *prism)
{
  return prism->api->synth();
}

static inline prism_power_state_t prism_power_state(prism_t *prism)
{
  return prism->api->power_state();
}

static inline void prism_sleep(prism_t *prism) { prism->api->sleep(); }
static inline void prism_system_reset(prism_t *prism)
{
  prism->api->system_reset();
}

static inline void prism_persist(prism_t *prism)
{
  if (prism->persistent != NULL && prism->api->persist != NULL)
    prism->api->persist();
}

static inline void prism_keep_awake(prism_t *prism)
{
  if (prism->api->keep_awake != NULL)
    prism->api->keep_awake();
}

/* Ask Prism to close the current cartridge and choose the normal Home
 * destination. The request is deferred until the lifecycle callback returns,
 * so packaged cartridges cannot invalidate their own execution image. */
static inline void prism_close(prism_t *prism)
{
  if (prism->api->close != NULL)
    prism->api->close();
}

static inline int prism_anim_to(prism_t *prism, volatile int32_t *subject,
                                int32_t target, uint32_t duration,
                                prism_anim_ease_t easing,
                                prism_anim_done_fn callback, void *user)
{
  return prism->api->anim_to(subject, target, duration, easing, callback, user);
}

static inline void prism_anim_cancel(prism_t *prism,
                                     volatile int32_t *subject, bool finish)
{
  prism->api->anim_cancel(subject, finish);
}

static inline bool prism_leaderboard_qrcode(prism_t *prism, uint8_t app_id,
                                            const void *data, size_t data_len,
                                            uint8_t *qrcode)
{
  return prism->api->leaderboard_qrcode(app_id, data, data_len, qrcode);
}

static inline uint32_t prism_asset_pack_count(prism_t *prism)
{
  return prism->api->asset_pack_count == NULL
             ? 0
             : prism->api->asset_pack_count();
}

static inline bool prism_asset_pack_get(prism_t *prism, uint32_t index,
                                        prism_asset_pack_info_t *info)
{
  return prism->api->asset_pack_get != NULL &&
         prism->api->asset_pack_get(index, info);
}

static inline bool prism_asset_file_get(prism_t *prism,
                                        prism_asset_pack_handle_t pack,
                                        uint32_t index,
                                        prism_asset_file_t *file)
{
  return prism->api->asset_file_get != NULL &&
         prism->api->asset_file_get(pack, index, file);
}
