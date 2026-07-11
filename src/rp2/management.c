#include "management.h"
#include "cartridge_storage.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <pico/stdio.h>
#include <pico/stdio/driver.h>
#include <pico/config.h>
#include <pico/sync.h>
#include <tusb.h>
#include <hardware/structs/watchdog.h>
#include <hardware/watchdog.h>

#include <platform/display.h>
#include <platform/identity.h>
#include <platform/input.h>
#include <platform/platform.h>
#include <platform/time.h>
#include <prism/management_protocol.h>
#include <prism/registry.h>
#include <prism/runtime.h>
#include <shared/engine.h>
#include <shared/os/settings.h>
#include <u8g2.h>

#define RX_BYTES (sizeof(prism_management_header_t) + PRISM_MANAGEMENT_MAX_PAYLOAD)
#define TX_BYTES 8192u
#define LOG_BYTES 2048u
#define HEARTBEAT_TIMEOUT_US 3000000u
#define MIRROR_INTERVAL_US 16667u

static uint8_t rx_buffer[RX_BYTES];
static size_t rx_length;

static uint8_t tx_buffer[TX_BYTES];
static size_t tx_read;
static size_t tx_write;
static size_t tx_count;

static uint8_t log_buffer[LOG_BYTES];
static size_t log_read;
static size_t log_write;
static size_t log_count;
static critical_section_t log_lock;

static bool session_active;
static bool mirror_subscribed;
static platform_time_t last_heartbeat;
static platform_time_t next_mirror;
static uint32_t mirror_sequence;
static bool boot_diagnostic_pending;
static char boot_diagnostic[96];
static bool processing_sleep_messages;
static bool sleep_wake_requested;

typedef enum
{
  MANAGEMENT_PROGRESS_NONE,
  MANAGEMENT_PROGRESS_INSTALL,
  MANAGEMENT_PROGRESS_DELETE,
  MANAGEMENT_PROGRESS_COMPACT,
} management_progress_operation_t;

static struct
{
  management_progress_operation_t operation;
  uint32_t completed;
  uint32_t total;
  bool finished;
  bool failed;
  platform_time_t dismiss_at;
  char name[PRISM_CARTRIDGE_NAME_MAX + 1];
  uint8_t icon[PRISM_CARTRIDGE_ICON_BYTES];
} progress;

static void queue_mirror(void);
static void drain_tx(void);

static void progress_tick(void)
{
  if (progress.finished && platform_time_reached(progress.dismiss_at))
  {
    progress.operation = MANAGEMENT_PROGRESS_NONE;
    engine_set_app(NULL);
  }
}

static void draw_centered(u8g2_t *u8g2, uint8_t x, uint8_t width,
                          uint8_t baseline, const char *text)
{
  uint8_t text_width = u8g2_GetStrWidth(u8g2, text);
  u8g2_DrawStr(u8g2, x + (width - text_width) / 2, baseline, text);
}

static void progress_frame(void)
{
  u8g2_t *u8g2 = platform_display_get_u8g2();
  /* This is a system-owned operation screen. Clear everything drawn earlier
   * in the frame, including the pause menu, before rendering the overlay. */
  u8g2_ClearBuffer(u8g2);
  const char *title = progress.operation == MANAGEMENT_PROGRESS_DELETE
                          ? "UNINSTALLING"
                      : progress.operation == MANAGEMENT_PROGRESS_COMPACT
                          ? "COMPACTING"
                          : "INSTALLING";
  const char *state = progress.failed ? "FAILED" :
                      progress.finished ? "DONE" : NULL;
  uint32_t percent = progress.total == 0 ? 0 :
      (progress.completed * 100u) / progress.total;
  if (percent > 100u)
    percent = 100u;

  bool has_cartridge = progress.operation == MANAGEMENT_PROGRESS_INSTALL &&
                       progress.name[0] != '\0';
  uint8_t content_x = has_cartridge ? 46 : 8;
  uint8_t content_width = has_cartridge ? 78 : 112;
  if (has_cartridge)
  {
    u8g2_DrawXBM(u8g2, 4, 14, PRISM_CARTRIDGE_ICON_WIDTH,
                 PRISM_CARTRIDGE_ICON_HEIGHT, progress.icon);
    u8g2_DrawRFrame(u8g2, 2, 12, 40, 40, 3);
  }
  u8g2_SetFont(u8g2, has_cartridge ? u8g2_font_4x6_tr :
                                     u8g2_font_5x7_tr);
  draw_centered(u8g2, has_cartridge ? content_x : 0,
                has_cartridge ? content_width : 128,
                has_cartridge ? 7 : 17, title);
  if (has_cartridge)
  {
    char line1[14] = {0};
    char line2[14] = {0};
    size_t length = strlen(progress.name);
    size_t split = length > 13 ? 13 : length;
    if (length > 13)
      for (size_t i = split; i > 0; --i)
        if (progress.name[i] == ' ')
        {
          split = i;
          break;
        }
    memcpy(line1, progress.name, split);
    size_t second = split;
    while (progress.name[second] == ' ')
      ++second;
    strncpy(line2, progress.name + second, sizeof(line2) - 1);
    if (length - second > sizeof(line2) - 1)
      memcpy(line2 + sizeof(line2) - 4, "...", 3);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    draw_centered(u8g2, content_x, content_width,
                  line2[0] == '\0' ? 23 : 18, line1);
    if (line2[0] != '\0')
      draw_centered(u8g2, content_x, content_width, 28, line2);
  }
  uint8_t bar_y = has_cartridge ? 34 : 27;
  u8g2_DrawRFrame(u8g2, content_x, bar_y, content_width, 13, 2);
  uint8_t inner_width = content_width - 4;
  uint8_t fill = (uint8_t)((percent * inner_width) / 100u);
  if (fill != 0)
    u8g2_DrawBox(u8g2, content_x + 2, bar_y + 2, fill, 9);

  char label[12];
  if (state != NULL)
    snprintf(label, sizeof(label), "%s", state);
  else
    snprintf(label, sizeof(label), "%lu%%", (unsigned long)percent);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  draw_centered(u8g2, has_cartridge ? content_x : 0,
                has_cartridge ? content_width : 128, 57, label);
}

static app_t progress_app = {
    .name = "management",
    .tick = progress_tick,
    /* The operation screen is rendered by management_task() after the engine
     * and pause-menu layers, so it always remains the topmost UI. */
    .frame = NULL,
};

static void progress_start(management_progress_operation_t operation,
                           uint32_t total, const char *name,
                           const uint8_t *icon)
{
  progress.operation = operation;
  progress.completed = 0;
  progress.total = total == 0 ? 1 : total;
  progress.finished = false;
  progress.failed = false;
  memset(progress.name, 0, sizeof(progress.name));
  memset(progress.icon, 0, sizeof(progress.icon));
  if (name != NULL)
    strncpy(progress.name, name, sizeof(progress.name) - 1);
  if (icon != NULL)
    memcpy(progress.icon, icon, sizeof(progress.icon));
  engine_set_app(&progress_app);
}

static void progress_finish(prism_management_status_t status)
{
  if (progress.operation == MANAGEMENT_PROGRESS_NONE)
    return;
  management_progress_operation_t operation = progress.operation;
  progress.failed = status != PRISM_MGMT_OK;
  if (!progress.failed)
    progress.completed = progress.total;
  progress.finished = true;
  /* A successful install is already reflected in the launcher. Show its
   * completed frame at most once, then transition on the next tick instead of
   * holding a redundant DONE screen. Keep errors (and the very fast delete
   * acknowledgement) visible long enough to read. */
  progress.dismiss_at = !progress.failed &&
                                operation != MANAGEMENT_PROGRESS_DELETE
                            ? platform_now_us()
                            : platform_time_add_ms(platform_now_us(), 750);
}

static void abort_install(void)
{
  cartridge_storage_install_abort();
  if (progress.operation != MANAGEMENT_PROGRESS_INSTALL)
    return;
  memset(&progress, 0, sizeof(progress));
  engine_set_app(NULL);
}

static void log_out(const char *bytes, int length)
{
  critical_section_enter_blocking(&log_lock);
  for (int i = 0; i < length; ++i)
  {
    if (log_count == LOG_BYTES)
    {
      log_read = (log_read + 1u) % LOG_BYTES;
      --log_count;
    }
    log_buffer[log_write] = (uint8_t)bytes[i];
    log_write = (log_write + 1u) % LOG_BYTES;
    ++log_count;
  }
  critical_section_exit(&log_lock);
}

static stdio_driver_t management_log_driver = {
    .out_chars = log_out,
};

static size_t tx_free(void) { return TX_BYTES - tx_count; }

static bool tx_push(const void *data, size_t length)
{
  if (length > tx_free())
    return false;
  const uint8_t *bytes = data;
  for (size_t i = 0; i < length; ++i)
  {
    tx_buffer[tx_write] = bytes[i];
    tx_write = (tx_write + 1u) % TX_BYTES;
  }
  tx_count += length;
  return true;
}

static bool queue_message(uint8_t type, uint16_t flags, uint32_t request_id,
                          const void *payload, uint32_t payload_len)
{
  prism_management_header_t header = {
      .magic = PRISM_MANAGEMENT_MAGIC,
      .version = PRISM_MANAGEMENT_VERSION,
      .type = type,
      .flags = flags,
      .request_id = request_id,
      .payload_len = payload_len,
  };
  if (sizeof(header) + payload_len > tx_free())
    return false;
  tx_push(&header, sizeof(header));
  return payload_len == 0 || tx_push(payload, payload_len);
}

static void queue_result(const prism_management_header_t *request,
                         prism_management_status_t status)
{
  prism_management_result_t result = {.status = status, .detail = 0};
  uint16_t flags = PRISM_MGMT_FLAG_RESPONSE;
  if (status != PRISM_MGMT_OK)
    flags |= PRISM_MGMT_FLAG_ERROR;
  queue_message(request->type, flags, request->request_id, &result,
                sizeof(result));
}

static void queue_device_info(const prism_management_header_t *request)
{
  prism_management_device_info_t info = {
      .protocol_version = PRISM_MANAGEMENT_VERSION,
      .firmware_major = 0,
      .firmware_minor = 2,
      .firmware_patch = 0,
      .flash_bytes = PICO_FLASH_SIZE_BYTES,
      .cartridge_block_bytes = PRISM_CARTRIDGE_BLOCK_BYTES,
      .capabilities = PRISM_CAP_MIRROR | PRISM_CAP_REMOTE_INPUT |
                      PRISM_CAP_LOGS | PRISM_CAP_SETTINGS |
                      PRISM_CAP_CARTRIDGES | PRISM_CAP_COMPACTION,
  };
  platform_device_id(info.serial);
  queue_message(request->type, PRISM_MGMT_FLAG_RESPONSE, request->request_id,
                &info, sizeof(info));
}

static void queue_cartridges(const prism_management_header_t *request)
{
  static uint8_t payload[sizeof(prism_management_cartridge_list_t) +
                         40 * sizeof(prism_management_cartridge_entry_t)];
  prism_management_cartridge_list_t *list = (void *)payload;
  list->count = 0;
  list->reserved = 0;
  size_t offset = sizeof(*list);

  for (size_t i = 0; i < prism_registry_count() && list->count < 40; ++i)
  {
    const prism_registry_entry_t *registry = prism_registry_get(i);
    if (registry == NULL ||
        (registry->policy & PRISM_REGISTRY_POLICY_BUNDLED) == 0)
      continue;
    prism_management_cartridge_entry_t entry = {0};
    entry.uuid[12] = (uint8_t)(registry->cartridge->app_id >> 24);
    entry.uuid[13] = (uint8_t)(registry->cartridge->app_id >> 16);
    entry.uuid[14] = (uint8_t)(registry->cartridge->app_id >> 8);
    entry.uuid[15] = (uint8_t)registry->cartridge->app_id;
    entry.policy = (uint16_t)registry->policy;
    strncpy(entry.slug, registry->cartridge->slug, sizeof(entry.slug) - 1);
    strncpy(entry.name, registry->cartridge->name, sizeof(entry.name) - 1);
    memcpy(payload + offset, &entry, sizeof(entry));
    offset += sizeof(entry);
    ++list->count;
  }

  for (size_t i = 0; i < cartridge_storage_count() && list->count < 40; ++i)
  {
    prism_management_cartridge_entry_t entry;
    if (!cartridge_storage_entry(i, &entry))
      continue;
    memcpy(payload + offset, &entry, sizeof(entry));
    offset += sizeof(entry);
    ++list->count;
  }

  queue_message(request->type, PRISM_MGMT_FLAG_RESPONSE, request->request_id,
                payload, (uint32_t)offset);
}

static void cartridge_uuid(const prism_registry_entry_t *entry,
                           uint8_t uuid[PRISM_CARTRIDGE_UUID_BYTES])
{
  memset(uuid, 0, PRISM_CARTRIDGE_UUID_BYTES);
  uuid[12] = (uint8_t)(entry->cartridge->app_id >> 24);
  uuid[13] = (uint8_t)(entry->cartridge->app_id >> 16);
  uuid[14] = (uint8_t)(entry->cartridge->app_id >> 8);
  uuid[15] = (uint8_t)entry->cartridge->app_id;
}

static void queue_cartridge_icon(const prism_management_header_t *request,
                                 const uint8_t *uuid)
{
  const prism_cartridge_t *cartridge = NULL;
  for (size_t i = 0; i < prism_registry_count(); ++i)
  {
    const prism_registry_entry_t *entry = prism_registry_get(i);
    if (entry == NULL ||
        (entry->policy & PRISM_REGISTRY_POLICY_BUNDLED) == 0)
      continue;
    uint8_t candidate[PRISM_CARTRIDGE_UUID_BYTES];
    cartridge_uuid(entry, candidate);
    if (memcmp(candidate, uuid, sizeof(candidate)) == 0)
    {
      cartridge = entry->cartridge;
      break;
    }
  }
  if (cartridge == NULL)
    cartridge = cartridge_storage_find_uuid(uuid);
  if (cartridge == NULL)
  {
    queue_result(request, PRISM_MGMT_ERROR_NOT_FOUND);
    return;
  }

  static uint8_t icon[PRISM_CARTRIDGE_ICON_BYTES];
  memset(icon, 0, sizeof(icon));
  if (cartridge->icon != NULL)
    memcpy(icon, cartridge->icon, sizeof(icon));
  queue_message(request->type, PRISM_MGMT_FLAG_RESPONSE, request->request_id,
                icon, sizeof(icon));
}

static void compact_progress(uint16_t completed, uint16_t total, void *user)
{
  (void)user;
  progress.completed = completed;
  progress.total = total == 0 ? 1 : total;
  engine_mark_input();
  platform_display_set_contrast(engine_output_brightness_scale());
  progress_frame();
  u8g2_SendBuffer(platform_display_get_u8g2());

  prism_management_progress_t update = {
      .operation = PRISM_OPERATION_COMPACT,
      .phase = completed >= total ? PRISM_OPERATION_PHASE_COMPLETE
                                  : PRISM_OPERATION_PHASE_RUNNING,
      .completed_blocks = completed,
      .total_blocks = total,
  };
  queue_message(PRISM_MGMT_OPERATION_PROGRESS, PRISM_MGMT_FLAG_EVENT, 0,
                &update, sizeof(update));

  /* Compaction monopolizes the normal frame callback. Pump TinyUSB and send
   * this checkpoint explicitly so both the website and mirror remain live
   * between block moves. */
  platform_task();
  drain_tx();
  if (mirror_subscribed && tx_count == 0)
    queue_mirror();
  drain_tx();
  platform_task();
}

static void handle_message(const prism_management_header_t *request,
                           const uint8_t *payload)
{
  session_active = true;
  prism_settings_set_save_deferred(true);
  prism_cartridge_persistence_set_deferred(true);
  last_heartbeat = platform_now_us();

  if (processing_sleep_messages)
  {
    switch ((prism_management_message_type_t)request->type)
    {
    case PRISM_MGMT_HEARTBEAT:
    case PRISM_MGMT_MIRROR_SUBSCRIBE:
    case PRISM_MGMT_MIRROR_UNSUBSCRIBE:
      break;
    case PRISM_MGMT_REMOTE_INPUT:
      if (request->payload_len == sizeof(prism_management_remote_input_t) &&
          ((const prism_management_remote_input_t *)payload)->buttons != 0)
        sleep_wake_requested = true;
      break;
    default:
      sleep_wake_requested = true;
      break;
    }
  }
  else
  {
    switch ((prism_management_message_type_t)request->type)
    {
    case PRISM_MGMT_HEARTBEAT:
    case PRISM_MGMT_MIRROR_SUBSCRIBE:
    case PRISM_MGMT_MIRROR_UNSUBSCRIBE:
      break;
    case PRISM_MGMT_REMOTE_INPUT:
      if (request->payload_len == sizeof(prism_management_remote_input_t) &&
          ((const prism_management_remote_input_t *)payload)->buttons != 0)
        engine_wake();
      break;
    default:
      engine_wake();
      break;
    }
  }

  switch ((prism_management_message_type_t)request->type)
  {
  case PRISM_MGMT_HELLO:
  case PRISM_MGMT_DEVICE_INFO:
    queue_device_info(request);
    break;
  case PRISM_MGMT_CARTRIDGE_LIST:
    queue_cartridges(request);
    break;
  case PRISM_MGMT_CARTRIDGE_ICON:
    if (request->payload_len != PRISM_CARTRIDGE_UUID_BYTES)
      queue_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
      queue_cartridge_icon(request, payload);
    break;
  case PRISM_MGMT_STORAGE_INFO:
  {
    prism_management_storage_info_t info;
    cartridge_storage_info(&info);
    queue_message(request->type, PRISM_MGMT_FLAG_RESPONSE,
                  request->request_id, &info, sizeof(info));
    break;
  }
  case PRISM_MGMT_SETTINGS_GET:
    queue_message(request->type, PRISM_MGMT_FLAG_RESPONSE, request->request_id,
                  prism_settings_get(), sizeof(prism_management_settings_t));
    break;
  case PRISM_MGMT_SETTINGS_PREVIEW:
    if (request->payload_len != sizeof(prism_management_settings_t) ||
        !prism_settings_preview((const void *)payload))
      queue_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
      queue_result(request, PRISM_MGMT_OK);
    break;
  case PRISM_MGMT_MIRROR_SUBSCRIBE:
    mirror_subscribed = true;
    next_mirror = platform_now_us();
    queue_result(request, PRISM_MGMT_OK);
    break;
  case PRISM_MGMT_MIRROR_UNSUBSCRIBE:
    mirror_subscribed = false;
    queue_result(request, PRISM_MGMT_OK);
    break;
  case PRISM_MGMT_REMOTE_INPUT:
    if (request->payload_len != sizeof(prism_management_remote_input_t))
      queue_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      const prism_management_remote_input_t *input = (const void *)payload;
      platform_input_set_remote_mask(input->buttons);
      queue_result(request, PRISM_MGMT_OK);
    }
    break;
  case PRISM_MGMT_HEARTBEAT:
  {
    prism_management_result_t result = {
        .status = PRISM_MGMT_OK,
        .detail = processing_sleep_messages ? PRISM_DEVICE_STATE_SLEEPING : 0,
    };
    queue_message(request->type, PRISM_MGMT_FLAG_RESPONSE,
                  request->request_id, &result, sizeof(result));
    break;
  }
  case PRISM_MGMT_CARTRIDGE_DELETE:
    if (request->payload_len != sizeof(prism_management_cartridge_id_t))
      queue_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      progress_start(MANAGEMENT_PROGRESS_DELETE, 1, NULL, NULL);
      prism_management_status_t status = cartridge_storage_delete(payload);
      progress_finish(status);
      queue_result(request, status);
    }
    break;
  case PRISM_MGMT_INSTALL_BEGIN:
    if (request->payload_len != sizeof(prism_management_install_begin_t))
      queue_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      prism_management_status_t status =
          cartridge_storage_install_begin((const void *)payload);
      if (status == PRISM_MGMT_OK)
      {
        const prism_management_install_begin_t *begin = (const void *)payload;
        progress_start(MANAGEMENT_PROGRESS_INSTALL, begin->package_bytes,
                       begin->name, begin->icon);
      }
      queue_result(request, status);
    }
    break;
  case PRISM_MGMT_INSTALL_CHUNK:
  {
    prism_management_status_t status = cartridge_storage_install_chunk(
        (const void *)payload, request->payload_len);
    if (status == PRISM_MGMT_OK && request->payload_len >= 8)
    {
      const prism_management_install_chunk_t *chunk = (const void *)payload;
      progress.completed = chunk->offset + chunk->data_len;
    }
    else if (status != PRISM_MGMT_OK)
    {
      progress_finish(status);
      cartridge_storage_install_abort();
    }
    queue_result(request, status);
    break;
  }
  case PRISM_MGMT_INSTALL_COMMIT:
    if (request->payload_len != 0)
      queue_result(request, PRISM_MGMT_ERROR_BAD_MESSAGE);
    else
    {
      prism_management_status_t status = cartridge_storage_install_commit();
      progress_finish(status);
      if (status != PRISM_MGMT_OK)
        cartridge_storage_install_abort();
      queue_result(request, status);
    }
    break;
  case PRISM_MGMT_COMPACT:
  {
    progress_start(MANAGEMENT_PROGRESS_COMPACT, 1, NULL, NULL);
    prism_management_status_t status =
        cartridge_storage_compact(compact_progress, NULL);
    progress_finish(status);
    queue_result(request, status);
    last_heartbeat = platform_now_us();
    break;
  }
  default:
    queue_result(request, PRISM_MGMT_ERROR_UNSUPPORTED);
    break;
  }
}

static void receive_messages(void)
{
  while (tud_vendor_available() && rx_length < sizeof(rx_buffer))
    rx_length += tud_vendor_read(rx_buffer + rx_length,
                                 (uint32_t)(sizeof(rx_buffer) - rx_length));

  for (;;)
  {
    if (rx_length < sizeof(prism_management_header_t))
      return;
    prism_management_header_t header;
    memcpy(&header, rx_buffer, sizeof(header));
    if (header.magic != PRISM_MANAGEMENT_MAGIC ||
        header.version != PRISM_MANAGEMENT_VERSION ||
        header.payload_len > PRISM_MANAGEMENT_MAX_PAYLOAD)
    {
      memmove(rx_buffer, rx_buffer + 1, --rx_length);
      continue;
    }
    size_t message_len = sizeof(header) + header.payload_len;
    if (rx_length < message_len)
      return;
    handle_message(&header, rx_buffer + sizeof(header));
    rx_length -= message_len;
    memmove(rx_buffer, rx_buffer + message_len, rx_length);
  }
}

static void queue_mirror(void)
{
  static prism_management_mirror_frame_t frame;
  frame.sequence = ++mirror_sequence;
  memcpy(frame.framebuffer,
         u8g2_GetBufferPtr(platform_display_get_u8g2()), PRISM_SCREEN_BYTES);
  for (uint8_t i = 0; i < 2; ++i)
  {
    color_t output = engine_led_output_color(i);
    frame.led_rgb[i][0] = output.r;
    frame.led_rgb[i][1] = output.g;
    frame.led_rgb[i][2] = output.b;
  }
  frame.buttons = (g_engine.buttons.left.pressed ? PRISM_REMOTE_LEFT : 0) |
                  (g_engine.buttons.right.pressed ? PRISM_REMOTE_RIGHT : 0) |
                  (g_engine.buttons.menu.pressed ? PRISM_REMOTE_MENU : 0);
  frame.reserved = 0;
  queue_message(PRISM_MGMT_MIRROR_FRAME, PRISM_MGMT_FLAG_EVENT, 0, &frame,
                sizeof(frame));
}

static void queue_logs(void)
{
  if (boot_diagnostic_pending)
  {
    size_t length = strlen(boot_diagnostic);
    if (tx_free() >= sizeof(prism_management_header_t) + length)
    {
      queue_message(PRISM_MGMT_LOG, PRISM_MGMT_FLAG_EVENT, 0,
                    boot_diagnostic, (uint32_t)length);
      boot_diagnostic_pending = false;
    }
    return;
  }
  uint8_t chunk[192];
  critical_section_enter_blocking(&log_lock);
  size_t length = log_count < sizeof(chunk) ? log_count : sizeof(chunk);
  if (length == 0 || tx_free() < sizeof(prism_management_header_t) + length)
  {
    critical_section_exit(&log_lock);
    return;
  }
  for (size_t i = 0; i < length; ++i)
  {
    chunk[i] = log_buffer[log_read];
    log_read = (log_read + 1u) % LOG_BYTES;
  }
  log_count -= length;
  critical_section_exit(&log_lock);
  queue_message(PRISM_MGMT_LOG, PRISM_MGMT_FLAG_EVENT, 0, chunk,
                (uint32_t)length);
}

static void drain_tx(void)
{
  while (tx_count > 0 && tud_vendor_write_available() > 0)
  {
    size_t contiguous = TX_BYTES - tx_read;
    if (contiguous > tx_count)
      contiguous = tx_count;
    uint32_t available = tud_vendor_write_available();
    if (contiguous > available)
      contiguous = available;
    uint32_t written = tud_vendor_write(tx_buffer + tx_read,
                                        (uint32_t)contiguous);
    if (written == 0)
      break;
    tx_read = (tx_read + written) % TX_BYTES;
    tx_count -= written;
  }
  tud_vendor_write_flush();
}

static void disconnect_session(void)
{
  abort_install();
  session_active = false;
  mirror_subscribed = false;
  platform_input_set_remote_mask(0);
  tx_read = tx_write = tx_count = 0;
  rx_length = 0;
  prism_settings_set_save_deferred(false);
  prism_cartridge_persistence_set_deferred(false);
}

void management_init(void)
{
  uint32_t previous_stage = watchdog_hw->scratch[0];
  uint32_t previous_detail = watchdog_hw->scratch[1];
  bool watchdog_reset = watchdog_caused_reboot();
  cartridge_storage_init();
  critical_section_init(&log_lock);
  stdio_set_driver_enabled(&management_log_driver, true);
  if (watchdog_reset || previous_stage != 0)
  {
    snprintf(boot_diagnostic, sizeof(boot_diagnostic),
             "boot diagnostic: watchdog=%u stage=%08lx detail=%08lx\n",
             watchdog_reset ? 1u : 0u, (unsigned long)previous_stage,
             (unsigned long)previous_detail);
    boot_diagnostic_pending = true;
    printf("%s", boot_diagnostic);
  }
  watchdog_hw->scratch[0] = 0;
  watchdog_hw->scratch[1] = 0;
  disconnect_session();
}

void management_task(void)
{
  if (!tud_mounted())
  {
    if (session_active)
      disconnect_session();
    return;
  }

  drain_tx();
  receive_messages();

  platform_time_t now = platform_now_us();
  if (session_active &&
      platform_time_diff_us(last_heartbeat, now) > HEARTBEAT_TIMEOUT_US)
  {
    disconnect_session();
    return;
  }

  if (session_active)
    queue_logs();

  /* Responses and logs are lossless. Move them into TinyUSB before producing
   * the next lossy mirror frame, and never let mirrors accumulate behind a
   * host that is temporarily stalled by a flash operation. */
  drain_tx();

  if (progress.operation != MANAGEMENT_PROGRESS_NONE)
  {
    /* USB management is active user interaction. Refresh this immediately
     * before the engine's end-of-frame sleep check, including while paused or
     * while a flash operation made the frame unusually long. */
    engine_mark_input();
    progress_frame();
  }
  if (mirror_subscribed && platform_time_reached(next_mirror))
  {
    if (tx_count == 0)
      queue_mirror();
    next_mirror = now + MIRROR_INTERVAL_US;
  }
  drain_tx();
}

bool management_sleep_task(void)
{
  if (!tud_mounted())
  {
    if (session_active)
      disconnect_session();
    return false;
  }

  sleep_wake_requested = false;
  processing_sleep_messages = true;
  drain_tx();
  receive_messages();
  processing_sleep_messages = false;
  /* Heartbeat and subscription responses must leave promptly; logs and mirror
   * frames intentionally remain dormant until the normal frame loop resumes. */
  drain_tx();
  return sleep_wake_requested;
}
