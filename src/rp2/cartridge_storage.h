#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/management_protocol.h>
#include <prism/asset_pack.h>
#include <platform/cartridge.h>

void cartridge_storage_init(void);
void cartridge_storage_info(prism_management_storage_info_t *info);
size_t cartridge_storage_count(void);
bool cartridge_storage_entry(size_t index,
                             prism_management_cartridge_entry_t *entry,
                             const char **id, const char **name);
size_t cartridge_storage_pack_count(void);
bool cartridge_storage_pack_entry(
    size_t index, prism_management_asset_pack_entry_t *entry,
    const char **id, const char **name, const char **target_id);
size_t cartridge_storage_asset_pack_count(
    const prism_cartridge_t *cartridge);
bool cartridge_storage_asset_pack_get(
    const prism_cartridge_t *cartridge, size_t index,
    prism_asset_pack_info_t *info);
bool cartridge_storage_asset_file_get(
    const prism_cartridge_t *cartridge, prism_asset_pack_handle_t pack,
    uint32_t index, prism_asset_file_t *file);
prism_management_status_t cartridge_storage_install_begin(
    const prism_management_install_begin_t *begin);
prism_management_status_t cartridge_storage_install_chunk(
    const prism_management_install_chunk_t *chunk, size_t payload_size);
prism_management_status_t cartridge_storage_install_commit(void);
void cartridge_storage_install_abort(void);
prism_management_status_t cartridge_storage_delete(
    const uint8_t app_key[PRISM_APP_KEY_BYTES]);
prism_management_status_t cartridge_storage_pack_delete(
    const uint8_t pack_key[PRISM_PACK_KEY_BYTES]);
typedef void (*cartridge_storage_progress_fn)(uint16_t completed_blocks,
                                              uint16_t total_blocks,
                                              void *user);
prism_management_status_t cartridge_storage_compact(
    cartridge_storage_progress_fn progress, void *user);
size_t cartridge_storage_installed_count(void);
const prism_cartridge_t *cartridge_storage_installed_get(size_t index);
const prism_cartridge_t *cartridge_storage_find_app_key(
    const uint8_t app_key[PRISM_APP_KEY_BYTES]);
bool cartridge_storage_prepare(const prism_cartridge_t *cartridge,
                               platform_cartridge_execution_t *execution);
void cartridge_storage_release_execution(
    platform_cartridge_execution_t *execution);
bool cartridge_storage_owns(const prism_cartridge_t *cartridge);
