#include "management_transport.h"

#include <string.h>

#include <pico/stdio.h>
#include <pico/stdio/driver.h>
#include <pico/sync.h>
#include <tusb.h>

#define RX_BYTES                                                            \
  (sizeof(prism_management_header_t) + PRISM_MANAGEMENT_MAX_PAYLOAD)
#define TX_BYTES 8192u
#define LOG_BYTES 2048u
#define LOG_CHUNK_BYTES 192u

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
static bool boot_diagnostic_pending;
static char boot_diagnostic[96];

static void capture_log(const char *bytes, int length)
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

static stdio_driver_t log_driver = {
    .out_chars = capture_log,
};

static size_t tx_free(void) { return TX_BYTES - tx_count; }

static void tx_push(const void *data, size_t length)
{
  const uint8_t *bytes = data;
  for (size_t i = 0; i < length; ++i)
  {
    tx_buffer[tx_write] = bytes[i];
    tx_write = (tx_write + 1u) % TX_BYTES;
  }
  tx_count += length;
}

void management_transport_init(void)
{
  critical_section_init(&log_lock);
  stdio_set_driver_enabled(&log_driver, true);
}

void management_transport_reset(void)
{
  tx_read = 0;
  tx_write = 0;
  tx_count = 0;
  rx_length = 0;
}

bool management_transport_queue(uint8_t type, uint16_t flags,
                                uint32_t request_id, const void *payload,
                                uint32_t payload_len)
{
  if (payload_len > PRISM_MANAGEMENT_MAX_PAYLOAD ||
      (payload_len != 0 && payload == NULL) ||
      sizeof(prism_management_header_t) + payload_len > tx_free())
    return false;
  prism_management_header_t header = {
      .magic = PRISM_MANAGEMENT_MAGIC,
      .version = PRISM_MANAGEMENT_VERSION,
      .type = type,
      .flags = flags,
      .request_id = request_id,
      .payload_len = payload_len,
  };
  tx_push(&header, sizeof(header));
  if (payload_len != 0)
    tx_push(payload, payload_len);
  return true;
}

void management_transport_result(const prism_management_header_t *request,
                                 prism_management_status_t status)
{
  prism_management_result_t result = {.status = status, .detail = 0};
  uint16_t flags = PRISM_MGMT_FLAG_RESPONSE;
  if (status != PRISM_MGMT_OK)
    flags |= PRISM_MGMT_FLAG_ERROR;
  management_transport_queue(request->type, flags, request->request_id,
                             &result, sizeof(result));
}

static bool valid_header(const prism_management_header_t *header)
{
  return header->magic == PRISM_MANAGEMENT_MAGIC &&
         header->version == PRISM_MANAGEMENT_VERSION &&
         header->payload_len <= PRISM_MANAGEMENT_MAX_PAYLOAD;
}

void management_transport_receive(management_message_fn handler, void *context)
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
    if (!valid_header(&header))
    {
      size_t discard = 1;
      while (rx_length - discard >= sizeof(header))
      {
        memcpy(&header, rx_buffer + discard, sizeof(header));
        if (valid_header(&header))
          break;
        ++discard;
      }
      rx_length -= discard;
      memmove(rx_buffer, rx_buffer + discard, rx_length);
      continue;
    }
    size_t message_length = sizeof(header) + header.payload_len;
    if (rx_length < message_length)
      return;
    handler(&header, rx_buffer + sizeof(header), context);
    rx_length -= message_length;
    memmove(rx_buffer, rx_buffer + message_length, rx_length);
  }
}

void management_transport_drain(void)
{
  while (tx_count > 0 && tud_vendor_write_available() > 0)
  {
    size_t contiguous = TX_BYTES - tx_read;
    if (contiguous > tx_count)
      contiguous = tx_count;
    uint32_t available = tud_vendor_write_available();
    if (contiguous > available)
      contiguous = available;
    uint32_t written =
        tud_vendor_write(tx_buffer + tx_read, (uint32_t)contiguous);
    if (written == 0)
      break;
    tx_read = (tx_read + written) % TX_BYTES;
    tx_count -= written;
  }
  tud_vendor_write_flush();
}

void management_transport_set_boot_diagnostic(const char *text)
{
  size_t length = strnlen(text, sizeof(boot_diagnostic) - 1u);
  memcpy(boot_diagnostic, text, length);
  boot_diagnostic[length] = '\0';
  boot_diagnostic_pending = true;
}

void management_transport_queue_logs(void)
{
  if (boot_diagnostic_pending)
  {
    size_t length = strlen(boot_diagnostic);
    if (management_transport_queue(PRISM_MGMT_LOG, PRISM_MGMT_FLAG_EVENT, 0,
                                   boot_diagnostic, (uint32_t)length))
      boot_diagnostic_pending = false;
    return;
  }

  uint8_t chunk[LOG_CHUNK_BYTES];
  critical_section_enter_blocking(&log_lock);
  size_t length = log_count < sizeof(chunk) ? log_count : sizeof(chunk);
  if (length == 0 ||
      tx_free() < sizeof(prism_management_header_t) + length)
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
  management_transport_queue(PRISM_MGMT_LOG, PRISM_MGMT_FLAG_EVENT, 0, chunk,
                             (uint32_t)length);
}

bool management_transport_empty(void) { return tx_count == 0; }
