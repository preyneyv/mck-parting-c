#include <shared/os/settings.h>

#include <string.h>

#include <platform/time.h>
#include <platform/persistence.h>
#include <platform/peripheral.h>
#include <shared/engine.h>
#include <shared/os/launcher.h>
#include <prism/graphics/color.h>

static prism_management_settings_t current;
static bool dirty;
static platform_time_t dirty_at;
static bool was_plugged_in;
static bool save_deferred;

#define SETTINGS_REVISION 2u

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
    settings.leds[i].effect = PRISM_LED_STATIC;
    settings.leds[i].palette_len = 1;
    settings.leds[i].speed_ms = 2000;
    settings.leds[i].phase_offset = i == 0 ? 0 : 128;
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
  prism_management_settings_t stored;
  bool loaded = platform_settings_load(&stored, sizeof(stored));
  bool valid = loaded && settings_valid(&stored);
  if (valid)
    current = stored;

  dirty = loaded && !valid;
  dirty_at = dirty ? platform_now_us() : PLATFORM_TIME_ZERO;
  was_plugged_in = platform_peripheral_get_power_state().plugged_in;
}

const prism_management_settings_t *prism_settings_get(void) { return &current; }

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
  uint32_t now_ms = (uint32_t)(platform_now_us() / 1000u);
  engine_led_set(LED_L, effect_color(&current.leds[0], now_ms));
  engine_led_set(LED_R, effect_color(&current.leds[1], now_ms));
}

void prism_settings_mark_saved(void) { dirty = false; }
bool prism_settings_is_dirty(void) { return dirty; }

void prism_settings_flush(void)
{
  if (dirty && platform_settings_save(&current, sizeof(current)))
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
