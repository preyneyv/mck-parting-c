#include <stdio.h>
#include <string.h>

#include <platform/persistence.h>
#include <platform/peripheral.h>
#include <platform/time.h>
#include <shared/engine.h>
#include <shared/os/launcher.h>
#include <shared/os/settings.h>

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
              #condition);                                                   \
      return 1;                                                              \
    }                                                                        \
  } while (0)

static uint8_t saved_settings[256];
static size_t saved_size;
static bool have_saved_settings;
static platform_time_t now_us = 1000;

app_t app_launcher;

bool platform_settings_load(void *data, size_t size)
{
  if (!have_saved_settings || size != saved_size)
    return false;
  memcpy(data, saved_settings, size);
  return true;
}

bool platform_settings_save(const void *data, size_t size)
{
  if (size > sizeof(saved_settings))
    return false;
  memcpy(saved_settings, data, size);
  saved_size = size;
  have_saved_settings = true;
  return true;
}

platform_time_t platform_now_us(void) { return now_us; }

platform_power_state_t platform_peripheral_get_power_state(void)
{
  return (platform_power_state_t){0};
}

bool engine_is_app(const app_t *app)
{
  (void)app;
  return false;
}

void engine_led_set(uint8_t led, color_t color)
{
  (void)led;
  (void)color;
}

void engine_set_volume(int8_t level) { (void)level; }
void engine_set_brightness(int8_t level) { (void)level; }

int main(void)
{
  prism_settings_init();
  CHECK(!prism_settings_first_interaction_complete());
  CHECK(prism_settings_guide_pending());

  prism_settings_complete_first_interaction();
  prism_settings_dismiss_guide();
  CHECK(prism_settings_is_dirty());
  prism_settings_flush();
  CHECK(!prism_settings_is_dirty());
  CHECK(saved_size > sizeof(prism_management_settings_t));

  prism_settings_init();
  CHECK(prism_settings_first_interaction_complete());
  CHECK(!prism_settings_guide_pending());

  prism_management_settings_t preview = *prism_settings_get();
  preview.volume = preview.volume == 8 ? 7 : (uint8_t)(preview.volume + 1);
  now_us += 1000;
  CHECK(prism_settings_preview(&preview));
  prism_settings_flush();
  prism_settings_init();
  CHECK(prism_settings_get()->volume == preview.volume);
  CHECK(prism_settings_first_interaction_complete());
  CHECK(!prism_settings_guide_pending());

  saved_settings[sizeof(prism_management_settings_t)] = 0x80;
  prism_settings_init();
  CHECK(!prism_settings_first_interaction_complete());
  CHECK(prism_settings_guide_pending());
  CHECK(prism_settings_is_dirty());

  puts("onboarding settings tests passed");
  return 0;
}
