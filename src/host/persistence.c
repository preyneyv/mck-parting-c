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

bool platform_cartridge_data_load(uint32_t app_id, uint16_t schema,
                                  void *data, size_t size)
{
  (void)app_id; (void)schema; (void)data; (void)size;
  return false;
}

bool platform_cartridge_data_save(uint32_t app_id, uint16_t schema,
                                  const void *data, size_t size)
{
  (void)app_id; (void)schema; (void)data; (void)size;
  return true;
}

bool platform_cartridge_data_delete(uint32_t app_id)
{
  (void)app_id;
  return true;
}
