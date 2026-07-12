#include "management_commands.h"

#include "cartridge_storage.h"
#include "management_overlay.h"
#include "management_protocol.h"
#include "management_transport.h"

#include <platform/display.h>
#include <platform/input.h>
#include <platform/platform.h>
#include <platform/time.h>
#include <prism/runtime.h>
#include <shared/engine.h>
#include <shared/os/settings.h>
#include <u8g2.h>

static void compact_progress(uint16_t completed, uint16_t total, void *user)
{
  management_session_t *session = user;
  management_overlay_set_progress(completed, total);
  engine_mark_input();
  platform_display_set_contrast(engine_output_brightness_scale());
  management_overlay_render();
  u8g2_SendBuffer(platform_display_get_u8g2());
  management_protocol_progress(completed, total);

  platform_task();
  management_transport_drain();
  if (session->mirror_subscribed && management_transport_empty())
    management_protocol_mirror();
  management_transport_drain();
  platform_task();
}

static void apply_wake_policy(const prism_management_header_t *request,
                              const uint8_t *payload,
                              management_session_t *session)
{
  bool passive = request->type == PRISM_MGMT_HEARTBEAT ||
                 request->type == PRISM_MGMT_MIRROR_SUBSCRIBE ||
                 request->type == PRISM_MGMT_MIRROR_UNSUBSCRIBE;
  bool pressed_remote = request->type == PRISM_MGMT_REMOTE_INPUT &&
                        request->payload_len ==
                            sizeof(prism_management_remote_input_t) &&
                        ((const prism_management_remote_input_t *)payload)
                                ->buttons != 0;
  if (session->processing_sleep_messages)
  {
    if (!passive &&
        (request->type != PRISM_MGMT_REMOTE_INPUT || pressed_remote))
      session->sleep_wake_requested = true;
  }
  else if (!passive &&
           (request->type != PRISM_MGMT_REMOTE_INPUT || pressed_remote))
    engine_wake();
}

static void queue_payload(const prism_management_header_t *request,
                          const void *payload, uint32_t payload_len)
{
  management_transport_queue(request->type, PRISM_MGMT_FLAG_RESPONSE,
                             request->request_id, payload, payload_len);
}

void management_commands_handle(const prism_management_header_t *request,
                                const uint8_t *payload,
                                management_session_t *session)
{
  session->active = true;
  prism_settings_set_save_deferred(true);
  prism_cartridge_persistence_set_deferred(true);
  session->last_heartbeat = platform_now_us();
  apply_wake_policy(request, payload, session);

  if (session->processing_sleep_messages &&
      request->type == PRISM_MGMT_COMPACT)
  {
    session->deferred_compact_request = *request;
    session->compact_deferred = true;
    return;
  }

  switch ((prism_management_message_type_t)request->type)
  {
  case PRISM_MGMT_HELLO:
  case PRISM_MGMT_DEVICE_INFO:
    management_protocol_device_info(request);
    break;
  case PRISM_MGMT_CARTRIDGE_LIST:
    if (request->payload_len != 0 &&
        request->payload_len !=
            sizeof(prism_management_cartridge_list_request_t))
      management_transport_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
      management_protocol_cartridges(
          request, request->payload_len == 0
                       ? 0
                       : ((const prism_management_cartridge_list_request_t *)
                              payload)
                             ->start_index);
    break;
  case PRISM_MGMT_CARTRIDGE_ICON:
    if (request->payload_len != PRISM_APP_KEY_BYTES)
      management_transport_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
      management_protocol_cartridge_icon(request, payload);
    break;
  case PRISM_MGMT_STORAGE_INFO:
  {
    prism_management_storage_info_t info;
    cartridge_storage_info(&info);
    queue_payload(request, &info, sizeof(info));
    break;
  }
  case PRISM_MGMT_SETTINGS_GET:
    queue_payload(request, prism_settings_get(),
                  sizeof(prism_management_settings_t));
    break;
  case PRISM_MGMT_SETTINGS_PREVIEW:
    management_transport_result(
        request,
        request->payload_len == sizeof(prism_management_settings_t) &&
                prism_settings_preview((const void *)payload)
            ? PRISM_MGMT_OK
            : PRISM_MGMT_ERROR_BAD_MESSAGE);
    break;
  case PRISM_MGMT_MIRROR_SUBSCRIBE:
    session->mirror_subscribed = true;
    session->next_mirror = platform_now_us();
    management_transport_result(request, PRISM_MGMT_OK);
    break;
  case PRISM_MGMT_MIRROR_UNSUBSCRIBE:
    session->mirror_subscribed = false;
    management_transport_result(request, PRISM_MGMT_OK);
    break;
  case PRISM_MGMT_REMOTE_INPUT:
    if (request->payload_len != sizeof(prism_management_remote_input_t))
      management_transport_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      const prism_management_remote_input_t *input = (const void *)payload;
      platform_input_set_remote_mask(input->buttons);
      management_transport_result(request, PRISM_MGMT_OK);
    }
    break;
  case PRISM_MGMT_HEARTBEAT:
  {
    prism_management_result_t result = {
        .status = PRISM_MGMT_OK,
        .detail = session->processing_sleep_messages
                      ? PRISM_DEVICE_STATE_SLEEPING
                      : 0,
    };
    queue_payload(request, &result, sizeof(result));
    break;
  }
  case PRISM_MGMT_CARTRIDGE_DELETE:
    if (request->payload_len != sizeof(prism_management_cartridge_id_t))
      management_transport_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      const prism_management_cartridge_id_t *id = (const void *)payload;
      const prism_cartridge_t *cartridge =
          cartridge_storage_find_app_key(id->app_key);
      management_overlay_start(MANAGEMENT_OVERLAY_DELETE, 1,
                               cartridge == NULL ? NULL : cartridge->name,
                               cartridge == NULL ? NULL : cartridge->icon);
      prism_management_status_t status =
          cartridge_storage_delete(id->app_key);
      management_overlay_finish(status);
      management_transport_result(request, status);
    }
    break;
  case PRISM_MGMT_INSTALL_BEGIN:
    if (request->payload_len != sizeof(prism_management_install_begin_t))
      management_transport_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      const prism_management_install_begin_t *begin = (const void *)payload;
      prism_management_status_t status =
          cartridge_storage_install_begin(begin);
      if (status == PRISM_MGMT_OK)
        management_overlay_start(MANAGEMENT_OVERLAY_INSTALL,
                                 begin->package_bytes, begin->name,
                                 begin->icon);
      management_transport_result(request, status);
    }
    break;
  case PRISM_MGMT_INSTALL_CHUNK:
  {
    prism_management_status_t status = cartridge_storage_install_chunk(
        (const void *)payload, request->payload_len);
    if (status == PRISM_MGMT_OK && request->payload_len >= 8)
    {
      const prism_management_install_chunk_t *chunk = (const void *)payload;
      management_overlay_set_completed(chunk->offset + chunk->data_len);
    }
    else if (status != PRISM_MGMT_OK)
    {
      management_overlay_finish(status);
      cartridge_storage_install_abort();
    }
    management_transport_result(request, status);
    break;
  }
  case PRISM_MGMT_INSTALL_COMMIT:
    if (request->payload_len != 0)
      management_transport_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      prism_management_status_t status = cartridge_storage_install_commit();
      management_overlay_finish(status);
      if (status != PRISM_MGMT_OK)
        cartridge_storage_install_abort();
      management_transport_result(request, status);
    }
    break;
  case PRISM_MGMT_COMPACT:
  {
    management_overlay_start(MANAGEMENT_OVERLAY_COMPACT, 1, NULL, NULL);
    prism_management_status_t status =
        cartridge_storage_compact(compact_progress, session);
    management_overlay_finish(status);
    management_transport_result(request, status);
    session->last_heartbeat = platform_now_us();
    break;
  }
  default:
    management_transport_result(request, PRISM_MGMT_ERROR_UNSUPPORTED);
    break;
  }
}
