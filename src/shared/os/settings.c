#include <shared/os/settings.h>

#include <string.h>

#include <platform/time.h>
#include <platform/persistence.h>
#include <platform/peripheral.h>
#include <shared/engine.h>
#include <shared/os/launcher.h>
#include <prism/graphics/color.h>

static prism_management_settings_t current;
static uint8_t onboarding_flags;
static bool dirty;
static platform_time_t dirty_at;
static bool was_plugged_in;
static bool save_deferred;

#define SETTINGS_REVISION 2u

enum
{
  ONBOARDING_FIRST_INTERACTION_COMPLETE = 1u << 0,
  ONBOARDING_GUIDE_PENDING = 1u << 1,
  ONBOARDING_FLAGS_VALID = ONBOARDING_FIRST_INTERACTION_COMPLETE |
                           ONBOARDING_GUIDE_PENDING,
};

typedef struct
{
  prism_management_settings_t management;
  uint8_t onboarding_flags;
  uint8_t reserved[3];
} persisted_settings_t;

_Static_assert(sizeof(persisted_settings_t) ==
                   sizeof(prism_management_settings_t) + 4u,
               "persisted settings layout must remain stable");

static color_t wire_color(const uint8_t rgb[3])
{
  return rgba(rgb[0], rgb[1], rgb[2], 255);
}

static color_t wheel(uint8_t position)
{
  if (position < 85)
    return rgba(255 - position * 3, position * 3, 0, 255);
  if (position < 170)
  {
    position -= 85;
    return rgba(0, 255 - position * 3, position * 3, 255);
  }
  position -= 170;
  return rgba(position * 3, 0, 255 - position * 3, 255);
}

static color_t effect_color(const prism_led_settings_t *led, uint32_t now_ms)
{
  uint32_t duration = led->speed_ms == 0 ? 2000u : led->speed_ms;
  uint8_t count = led->palette_len;
  if (count == 0)
    count = 1;

  switch ((prism_led_effect_t)led->effect)
  {
  case PRISM_LED_BREATHING:
  {
    uint32_t phase = now_ms % duration;
    uint32_t half = duration / 2u;
    if (half == 0)
      half = 1;
    uint8_t level = phase < half ? (uint8_t)(phase * 255u / half)
                                 : (uint8_t)((duration - phase) * 255u / half);
    return color_scale(wire_color(led->colors[0]), level / 255.0f);
  }
  case PRISM_LED_CROSSFADE:
  {
    if (count == 1)
      return wire_color(led->colors[0]);
    uint32_t span = duration / count;
    if (span == 0)
      span = 1;
    uint32_t phase = now_ms % duration;
    uint8_t from = (uint8_t)((phase / span) % count);
    uint8_t to = (uint8_t)((from + 1u) % count);
    return color_lerp(wire_color(led->colors[from]),
                      wire_color(led->colors[to]),
                      (phase % span) / (float)span);
  }
  case PRISM_LED_RAINBOW:
    return wheel((uint8_t)((now_ms * 256u / duration) + led->phase_offset));
  case PRISM_LED_STATIC:
  default:
    return wire_color(led->colors[0]);
  }
}

static prism_management_settings_t default_settings(void)
{
  prism_management_settings_t settings = {
      .volume = 4,
      .linked_leds = 1,
      .brightness = 8,
      .settings_revision = SETTINGS_REVISION,
  };
  for (uint8_t i = 0; i < 2; ++i)
  {
    settings.leds[i].effect = PRISM_LED_RAINBOW;
    settings.leds[i].palette_len = 1;
    settings.leds[i].speed_ms = 8000;
    settings.leds[i].phase_offset = i == 0 ? 0 : 14;
    settings.leds[i].colors[0][0] = 24;
    settings.leds[i].colors[0][1] = 96;
    settings.leds[i].colors[0][2] = 255;
  }
  return settings;
}

static bool settings_valid(const prism_management_settings_t *settings)
{
  if (settings->settings_revision != SETTINGS_REVISION ||
      settings->volume > 8 || settings->brightness > 8)
    return false;
  for (uint8_t i = 0; i < 2; ++i)
    if (settings->leds[i].effect > PRISM_LED_RAINBOW ||
        settings->leds[i].palette_len > PRISM_LED_PALETTE_MAX)
      return false;
  return true;
}

void prism_settings_init(void)
{
  current = default_settings();
  onboarding_flags = ONBOARDING_GUIDE_PENDING;
  persisted_settings_t stored;
  bool loaded = platform_settings_load(&stored, sizeof(stored));
  bool valid = loaded && settings_valid(&stored.management) &&
               (stored.onboarding_flags & ~ONBOARDING_FLAGS_VALID) == 0;
  if (valid)
  {
    current = stored.management;
    onboarding_flags = stored.onboarding_flags;
  }

  dirty = loaded && !valid;
  dirty_at = dirty ? platform_now_us() : PLATFORM_TIME_ZERO;
  was_plugged_in = platform_peripheral_get_power_state().plugged_in;
}

const prism_management_settings_t *prism_settings_get(void) { return &current; }

color_t prism_settings_led_color(uint8_t led)
{
  if (led >= 2)
    return (color_t){.hex = 0};
  uint32_t now_ms = (uint32_t)(platform_now_us() / 1000u);
  return effect_color(&current.leds[led], now_ms);
}

bool prism_settings_preview(const prism_management_settings_t *settings)
{
  if (settings == NULL || !settings_valid(settings))
    return false;

  current = *settings;
  if (current.linked_leds)
  {
    uint8_t phase_offset = current.leds[1].phase_offset;
    current.leds[1] = current.leds[0];
    current.leds[0].phase_offset = 0;
    current.leds[1].phase_offset = phase_offset;
  }
  else
  {
    current.leds[0].phase_offset = 0;
    current.leds[1].phase_offset = 0;
  }
  engine_set_volume((int8_t)current.volume);
  engine_set_brightness((int8_t)current.brightness);
  dirty = true;
  dirty_at = platform_now_us();
  return true;
}

void prism_settings_frame(void)
{
  if (!engine_is_app(&app_launcher))
    return;
  engine_led_set(LED_L, prism_settings_led_color(LED_L));
  engine_led_set(LED_R, prism_settings_led_color(LED_R));
}

void prism_settings_mark_saved(void) { dirty = false; }
bool prism_settings_is_dirty(void) { return dirty; }

void prism_settings_flush(void)
{
  persisted_settings_t stored = {
      .management = current,
      .onboarding_flags = onboarding_flags,
  };
  if (dirty && platform_settings_save(&stored, sizeof(stored)))
    prism_settings_mark_saved();
}

void prism_settings_task(void)
{
  bool plugged_in = platform_peripheral_get_power_state().plugged_in;
  if (dirty && !save_deferred &&
      ((dirty_at != PLATFORM_TIME_ZERO &&
                 platform_time_diff_us(dirty_at, platform_now_us()) >=
                     10000000) ||
                (was_plugged_in && !plugged_in)))
    prism_settings_flush();
  was_plugged_in = plugged_in;
}

void prism_settings_set_save_deferred(bool deferred)
{
  save_deferred = deferred;
}

void prism_settings_volume_changed(uint8_t volume)
{
  if (current.volume == volume)
    return;
  current.volume = volume;
  dirty = true;
  dirty_at = platform_now_us();
}

void prism_settings_brightness_changed(uint8_t brightness)
{
  if (current.brightness == brightness)
    return;
  current.brightness = brightness;
  current.settings_revision = SETTINGS_REVISION;
  dirty = true;
  dirty_at = platform_now_us();
}

bool prism_settings_first_interaction_complete(void)
{
  return (onboarding_flags & ONBOARDING_FIRST_INTERACTION_COMPLETE) != 0;
}

bool prism_settings_guide_pending(void)
{
  return (onboarding_flags & ONBOARDING_GUIDE_PENDING) != 0;
}

void prism_settings_complete_first_interaction(void)
{
  if (prism_settings_first_interaction_complete())
    return;
  onboarding_flags |= ONBOARDING_FIRST_INTERACTION_COMPLETE;
  dirty = true;
  dirty_at = platform_now_us();
}

void prism_settings_dismiss_guide(void)
{
  if (!prism_settings_guide_pending())
    return;
  onboarding_flags &= (uint8_t)~ONBOARDING_GUIDE_PENDING;
  dirty = true;
  dirty_at = platform_now_us();
}
