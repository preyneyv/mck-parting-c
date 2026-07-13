#include "management_protocol.h"

#include "cartridge_storage.h"
#include "management_transport.h"

#include <string.h>

#include <pico/config.h>

#include <platform/display.h>
#include <platform/identity.h>
#include <prism/cartridge_identity.h>
#include <prism/management_serialization.h>
#include <prism/registry.h>
#include <shared/engine.h>
#include <u8g2.h>

void management_protocol_device_info(
    const prism_management_header_t *request)
{
  prism_management_device_info_t info = {
      .protocol_version = PRISM_MANAGEMENT_VERSION,
      .firmware_major = 1,
      .firmware_minor = 0,
      .firmware_patch = 0,
      .flash_bytes = PICO_FLASH_SIZE_BYTES,
      .storage_block_bytes = PRISM_STORAGE_BLOCK_BYTES,
      .capabilities = PRISM_CAP_MIRROR | PRISM_CAP_REMOTE_INPUT |
                      PRISM_CAP_LOGS | PRISM_CAP_SETTINGS |
                      PRISM_CAP_CARTRIDGES | PRISM_CAP_COMPACTION |
                      PRISM_CAP_APP_LAUNCH | PRISM_CAP_REBOOT |
                      PRISM_CAP_ASSET_PACKS,
  };
  platform_device_id(info.serial);
  management_transport_queue(request->type, PRISM_MGMT_FLAG_RESPONSE,
                             request->request_id, &info, sizeof(info));
}

void management_protocol_asset_packs(
    const prism_management_header_t *request, uint16_t start_index)
{
  static uint8_t payload[PRISM_MANAGEMENT_MAX_PAYLOAD];
  size_t count = cartridge_storage_pack_count();
  uint16_t total_count = count > UINT16_MAX ? UINT16_MAX : (uint16_t)count;
  if (start_index > total_count)
    start_index = total_count;
  prism_management_asset_pack_list_init(payload, sizeof(payload),
                                         total_count, start_index);
  for (size_t i = start_index; i < count; ++i)
  {
    prism_management_asset_pack_entry_t entry;
    const char *id;
    const char *name;
    const char *target_id;
    if (!cartridge_storage_pack_entry(i, &entry, &id, &name, &target_id))
      continue;
    if (!prism_management_asset_pack_list_append(
            payload, sizeof(payload), &entry, id, name, target_id))
      break;
  }
  management_transport_queue(
      request->type, PRISM_MGMT_FLAG_RESPONSE, request->request_id, payload,
      (uint32_t)prism_management_asset_pack_list_size(payload));
}

void management_protocol_cartridges(
    const prism_management_header_t *request, uint16_t start_index,
    uint16_t flags)
{
  static uint8_t payload[PRISM_MANAGEMENT_MAX_PAYLOAD];
  bool include_hidden =
      (flags & PRISM_CARTRIDGE_LIST_INCLUDE_HIDDEN) != 0;
  uint16_t bundled_count = 0;
  for (size_t i = 0; i < prism_registry_count(); ++i)
  {
    const prism_registry_entry_t *registry = prism_registry_get(i);
    if (registry != NULL &&
        (registry->policy & PRISM_REGISTRY_POLICY_BUNDLED) != 0 &&
        (include_hidden ||
         (registry->policy & PRISM_REGISTRY_POLICY_HIDDEN) == 0))
      ++bundled_count;
  }
  size_t installed_count = cartridge_storage_count();
  size_t total_size = bundled_count + installed_count;
  uint16_t total_count = total_size > UINT16_MAX ? UINT16_MAX
                                                  : (uint16_t)total_size;
  if (start_index > total_count)
    start_index = total_count;
  prism_management_cartridge_list_init(payload, sizeof(payload), total_count,
                                        start_index);
  uint16_t logical_index = 0;
  bool full = false;

  for (size_t i = 0; i < prism_registry_count() && !full; ++i)
  {
    const prism_registry_entry_t *registry = prism_registry_get(i);
    if (registry == NULL ||
        (registry->policy & PRISM_REGISTRY_POLICY_BUNDLED) == 0 ||
        (!include_hidden &&
         (registry->policy & PRISM_REGISTRY_POLICY_HIDDEN) != 0))
      continue;
    if (logical_index++ < start_index)
      continue;
    prism_management_cartridge_entry_t entry = {0};
    if (!prism_app_key_derive(registry->cartridge->id, entry.app_key))
      continue;
    entry.persistent_bytes = (uint32_t)registry->cartridge->persistent_size;
    entry.version = registry->cartridge->version;
    entry.policy = (uint16_t)registry->policy;
    full = !prism_management_cartridge_list_append(
        payload, sizeof(payload), &entry, registry->cartridge->id,
        registry->cartridge->name);
  }

  for (size_t i = 0; i < installed_count && !full; ++i)
  {
    if (logical_index++ < start_index)
      continue;
    prism_management_cartridge_entry_t entry;
    const char *id;
    const char *name;
    if (!cartridge_storage_entry(i, &entry, &id, &name))
      continue;
    full = !prism_management_cartridge_list_append(
        payload, sizeof(payload), &entry, id, name);
  }

  management_transport_queue(request->type, PRISM_MGMT_FLAG_RESPONSE,
                             request->request_id, payload,
                             (uint32_t)prism_management_cartridge_list_size(
                                 payload));
}

void management_protocol_cartridge_icon(
    const prism_management_header_t *request, const uint8_t *app_key)
{
  const prism_cartridge_t *cartridge = NULL;
  for (size_t i = 0; i < prism_registry_count(); ++i)
  {
    const prism_registry_entry_t *entry = prism_registry_get(i);
    if (entry == NULL ||
        (entry->policy & PRISM_REGISTRY_POLICY_BUNDLED) == 0)
      continue;
    prism_app_key_t candidate;
    if (prism_app_key_derive(entry->cartridge->id, candidate) &&
        memcmp(candidate, app_key, sizeof(candidate)) == 0)
    {
      cartridge = entry->cartridge;
      break;
    }
  }
  if (cartridge == NULL)
    cartridge = cartridge_storage_find_app_key(app_key);
  if (cartridge == NULL)
  {
    management_transport_result(request, PRISM_MGMT_ERROR_NOT_FOUND);
    return;
  }

  static uint8_t icon[PRISM_CARTRIDGE_ICON_BYTES];
  memset(icon, 0, sizeof(icon));
  if (cartridge->icon != NULL)
    memcpy(icon, cartridge->icon, sizeof(icon));
  management_transport_queue(request->type, PRISM_MGMT_FLAG_RESPONSE,
                             request->request_id, icon, sizeof(icon));
}

void management_protocol_mirror(void)
{
  static prism_management_mirror_frame_t frame;
  frame.sequence++;
  memcpy(frame.framebuffer,
         u8g2_GetBufferPtr(platform_display_get_u8g2()), PRISM_SCREEN_BYTES);
  for (uint8_t i = 0; i < 2; ++i)
  {
    color_t output = engine_led_color(i);
    frame.led_rgb[i][0] = output.r;
    frame.led_rgb[i][1] = output.g;
    frame.led_rgb[i][2] = output.b;
  }
  frame.buttons =
      (engine_button_pressed(BUTTON_LEFT) ? PRISM_REMOTE_LEFT : 0) |
      (engine_button_pressed(BUTTON_RIGHT) ? PRISM_REMOTE_RIGHT : 0) |
      (engine_button_pressed(BUTTON_MENU) ? PRISM_REMOTE_MENU : 0);
  frame.reserved = 0;
  management_transport_queue(PRISM_MGMT_MIRROR_FRAME, PRISM_MGMT_FLAG_EVENT,
                             0, &frame, sizeof(frame));
}

void management_protocol_progress(uint16_t completed, uint16_t total)
{
  prism_management_progress_t update = {
      .operation = PRISM_OPERATION_COMPACT,
      .phase = completed >= total ? PRISM_OPERATION_PHASE_COMPLETE
                                  : PRISM_OPERATION_PHASE_RUNNING,
      .completed_blocks = completed,
      .total_blocks = total,
  };
  management_transport_queue(PRISM_MGMT_OPERATION_PROGRESS,
                             PRISM_MGMT_FLAG_EVENT, 0, &update,
                             sizeof(update));
}
