#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t prism_time_t;

/* Prism's integer application clock and input sampler run at this rate.
 * Cartridge tick callbacks may divide this clock, but prism_ticks() always
 * reports these base ticks. */
#define PRISM_ENGINE_TICK_RATE 960u

static inline uint32_t prism_ticks_from_ms(uint32_t milliseconds)
{
  return (uint32_t)(((uint64_t)milliseconds * PRISM_ENGINE_TICK_RATE + 999u) /
                    1000u);
}

static inline uint32_t prism_ms_from_ticks(uint32_t ticks)
{
  return (uint32_t)(((uint64_t)ticks * 1000u) / PRISM_ENGINE_TICK_RATE);
}

#define PRISM_CARTRIDGE_ICON_WIDTH 36u
#define PRISM_CARTRIDGE_ICON_HEIGHT 36u
#define PRISM_CARTRIDGE_ICON_BYTES                                        \
  (((PRISM_CARTRIDGE_ICON_WIDTH + 7u) / 8u) * PRISM_CARTRIDGE_ICON_HEIGHT)

#define PRISM_TIME_ZERO ((prism_time_t)0)
#define PRISM_TIME_END ((prism_time_t)UINT64_MAX)

typedef union
{
  struct __attribute__((packed))
  {
    uint8_t b;
    uint8_t g;
    uint8_t r;
    uint8_t a;
  };
  uint32_t hex;
} prism_color_t;

static inline prism_color_t prism_rgba(uint8_t r, uint8_t g, uint8_t b,
                                       uint8_t a)
{
  return (prism_color_t){.r = r, .g = g, .b = b, .a = a};
}

typedef struct
{
  bool plugged_in;
  bool charging;
  uint8_t battery_level;
} prism_power_state_t;

typedef uint8_t prism_anim_ease_t;
enum
{
  PRISM_ANIM_EASE_LINEAR,
  PRISM_ANIM_EASE_INOUT_QUAD,
  PRISM_ANIM_EASE_OUT_CUBIC,
};

typedef void (*prism_anim_done_fn)(void *user);

_Static_assert(sizeof(prism_time_t) == 8, "prism_time_t is part of the ABI");
_Static_assert(sizeof(prism_color_t) == 4, "prism_color_t is part of the ABI");
_Static_assert(offsetof(prism_color_t, b) == 0, "prism_color_t layout changed");
_Static_assert(offsetof(prism_color_t, g) == 1, "prism_color_t layout changed");
_Static_assert(offsetof(prism_color_t, r) == 2, "prism_color_t layout changed");
_Static_assert(offsetof(prism_color_t, a) == 3, "prism_color_t layout changed");
_Static_assert(sizeof(prism_power_state_t) == 3,
               "prism_power_state_t is part of the ABI");
_Static_assert(offsetof(prism_power_state_t, battery_level) == 2,
               "prism_power_state_t layout changed");
_Static_assert(sizeof(prism_anim_ease_t) == 1,
               "prism_anim_ease_t is part of the ABI");
