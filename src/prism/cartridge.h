#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <u8g2.h>

#include <platform/leds.h>
#include <platform/time.h>
#include <platform/peripheral.h>
#include <shared/anim.h>
#include <shared/audio/synth.h>
#include <shared/leaderboard/leaderboard.h>

#define PRISM_CARTRIDGE_MAGIC 0x50524354u /* PRCT */
#define PRISM_CARTRIDGE_ABI_V1 1u

typedef enum
{
  PRISM_BUTTON_NONE = 0,
  PRISM_BUTTON_LEFT = 1,
  PRISM_BUTTON_RIGHT = 2,
  PRISM_BUTTON_MENU = 3,
} prism_button_t;

typedef enum
{
  PRISM_CARTRIDGE_FLAG_NONE = 0,
} prism_cartridge_flags_t;

typedef struct prism_api_v1
{
  uint32_t abi_version;
  uint32_t struct_size;

  u8g2_t *(*display)(void);
  uint32_t (*ticks)(void);
  platform_time_t (*now_us)(void);
  int64_t (*time_diff_us)(platform_time_t from, platform_time_t to);

  bool (*button_pressed)(prism_button_t button);
  bool (*button_edge)(prism_button_t button);
  float (*button_hold_ratio)(prism_button_t button);
  void (*buttons_reset)(void);

  void (*led_set)(uint8_t led, color_t color);
  audio_synth_t *(*synth)(void);
  platform_power_state_t (*power_state)(void);
  void (*sleep)(void);
  void (*system_reset)(void);
  void (*persist)(void);
  void (*keep_awake)(void);

  int (*anim_to)(volatile int32_t *subject, int32_t target,
                 uint32_t duration, anim_ease_t easing,
                 anim_done_fn callback, void *user);
  void (*anim_cancel)(volatile int32_t *subject, int finish);

  bool (*leaderboard_qrcode)(uint8_t app_id, void *data, size_t data_len,
                             uint8_t *qrcode);
} prism_api_v1_t;

typedef struct prism_context
{
  const prism_api_v1_t *api;
  const struct prism_cartridge *cartridge;
  void *state;
  size_t state_size;
  void *persistent;
  size_t persistent_size;
} prism_t;

typedef void (*prism_lifecycle_fn)(prism_t *prism);

typedef struct prism_cartridge
{
  uint32_t magic;
  uint16_t abi_version;
  uint16_t descriptor_size;
  uint32_t flags;
  uint32_t app_id;
  const char *slug;
  const char *name;
  const uint8_t *icon;
  size_t state_size;

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

#if defined(__GNUC__)
#define PRISM_CARTRIDGE_EXPORT                                                \
  __attribute__((used, aligned(4), section(".prism_cartridge")))
#else
#define PRISM_CARTRIDGE_EXPORT
#endif

#define PRISM_CARTRIDGE_INTERNAL(                                             \
    _symbol, _app_id, _slug, _name, _icon, _flags, _state_size,              \
    _persistent_size, _persistent_schema, _enter, _tick, _frame, _pause,     \
    _resume, _leave)                                                          \
  PRISM_CARTRIDGE_EXPORT const prism_cartridge_t _symbol = {                  \
      .magic = PRISM_CARTRIDGE_MAGIC,                                         \
      .abi_version = PRISM_CARTRIDGE_ABI_V1,                                  \
      .descriptor_size = sizeof(prism_cartridge_t),                           \
      .flags = (_flags),                                                       \
      .app_id = (_app_id),                                                     \
      .slug = (_slug),                                                         \
      .name = (_name),                                                         \
      .icon = (_icon),                                                         \
      .state_size = (_state_size),                                             \
      .enter = (_enter),                                                       \
      .tick = (_tick),                                                         \
      .frame = (_frame),                                                       \
      .pause = (_pause),                                                       \
      .resume = (_resume),                                                     \
      .leave = (_leave),                                                       \
      .persistent_size = (_persistent_size),                                  \
      .persistent_schema_version = (_persistent_schema),                      \
  }

#define PRISM_CARTRIDGE(_symbol, _app_id, _slug, _name, _icon, _flags,       \
                        _state_size, _enter, _tick, _frame, _pause, _resume, \
                        _leave)                                               \
  PRISM_CARTRIDGE_INTERNAL(                                                   \
      _symbol, _app_id, _slug, _name, _icon, _flags, _state_size, 0, 0,     \
      _enter, _tick, _frame, _pause, _resume, _leave)

#define PRISM_CARTRIDGE_PERSISTENT(                                           \
    _symbol, _app_id, _slug, _name, _icon, _flags, _state_size,              \
    _persistent_type, _persistent_schema, _enter, _tick, _frame, _pause,     \
    _resume, _leave)                                                          \
  PRISM_CARTRIDGE_INTERNAL(                                                   \
      _symbol, _app_id, _slug, _name, _icon, _flags, _state_size,            \
      sizeof(_persistent_type), _persistent_schema, _enter, _tick, _frame,   \
      _pause, _resume, _leave)

static inline u8g2_t *prism_display(prism_t *prism)
{
  return prism->api->display();
}

static inline uint32_t prism_ticks(prism_t *prism)
{
  return prism->api->ticks();
}

static inline platform_time_t prism_now_us(prism_t *prism)
{
  return prism->api->now_us();
}

static inline int64_t prism_time_diff_us(prism_t *prism, platform_time_t from,
                                         platform_time_t to)
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
  return prism_button_pressed(prism, button) && prism_button_edge(prism, button);
}

static inline bool prism_button_keyup(prism_t *prism, prism_button_t button)
{
  return !prism_button_pressed(prism, button) && prism_button_edge(prism, button);
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

static inline void prism_led_set(prism_t *prism, uint8_t led, color_t color)
{
  prism->api->led_set(led, color);
}

static inline audio_synth_t *prism_synth(prism_t *prism)
{
  return prism->api->synth();
}

static inline platform_power_state_t prism_power_state(prism_t *prism)
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

static inline int prism_anim_to(prism_t *prism, volatile int32_t *subject,
                                int32_t target, uint32_t duration,
                                anim_ease_t easing,
                                anim_done_fn callback, void *user)
{
  return prism->api->anim_to(subject, target, duration, easing, callback, user);
}

static inline void prism_anim_cancel(prism_t *prism,
                                     volatile int32_t *subject, bool finish)
{
  prism->api->anim_cancel(subject, finish);
}

static inline bool prism_leaderboard_qrcode(prism_t *prism, uint8_t app_id,
                                            void *data, size_t data_len,
                                            uint8_t *qrcode)
{
  return prism->api->leaderboard_qrcode(app_id, data, data_len, qrcode);
}
