#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/asset_pack.h>
#include <prism/cartridge.h>

size_t platform_asset_pack_count(const prism_cartridge_t *cartridge);
bool platform_asset_pack_get(const prism_cartridge_t *cartridge, size_t index,
                             prism_asset_pack_info_t *info);
bool platform_asset_file_get(const prism_cartridge_t *cartridge,
                             prism_asset_pack_handle_t pack, uint32_t index,
                             prism_asset_file_t *file);
