#include <platform/persistence.h>

bool platform_settings_load(void *data, size_t size)
{
  (void)data;
  (void)size;
  return false;
}

bool platform_settings_save(const void *data, size_t size)
{
  (void)data;
  (void)size;
  return true;
}

bool platform_cartridge_data_load(const uint8_t app_key[PRISM_APP_KEY_BYTES],
                                  uint16_t schema,
                                  void *data, size_t size)
{
  (void)app_key; (void)schema; (void)data; (void)size;
  return false;
}

bool platform_cartridge_data_save(const uint8_t app_key[PRISM_APP_KEY_BYTES],
                                  uint16_t schema,
                                  const void *data, size_t size)
{
  (void)app_key; (void)schema; (void)data; (void)size;
  return true;
}

bool platform_cartridge_data_delete(
    const uint8_t app_key[PRISM_APP_KEY_BYTES])
{
  (void)app_key;
  return true;
}
