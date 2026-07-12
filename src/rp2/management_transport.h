#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/management_protocol.h>

typedef void (*management_message_fn)(const prism_management_header_t *header,
                                      const uint8_t *payload, void *context);

void management_transport_init(void);
void management_transport_reset(void);
void management_transport_receive(management_message_fn handler,
                                  void *context);
void management_transport_drain(void);
void management_transport_queue_logs(void);
void management_transport_set_boot_diagnostic(const char *text);
bool management_transport_empty(void);
bool management_transport_queue(uint8_t type, uint16_t flags,
                                uint32_t request_id, const void *payload,
                                uint32_t payload_len);
void management_transport_result(const prism_management_header_t *request,
                                 prism_management_status_t status);
