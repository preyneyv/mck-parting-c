#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool platform_settings_load(void *data, size_t size);
bool platform_settings_save(const void *data, size_t size);
bool platform_cartridge_data_load(uint32_t app_id, uint16_t schema,
                                  void *data, size_t size);
bool platform_cartridge_data_save(uint32_t app_id, uint16_t schema,
                                  const void *data, size_t size);
bool platform_cartridge_data_delete(uint32_t app_id);
