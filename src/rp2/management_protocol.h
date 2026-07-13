#pragma once

#include <stdint.h>

#include <prism/management_protocol.h>

void management_protocol_device_info(
    const prism_management_header_t *request);
void management_protocol_cartridges(
    const prism_management_header_t *request, uint16_t start_index,
    uint16_t flags);
void management_protocol_cartridge_icon(
    const prism_management_header_t *request, const uint8_t *app_key);
void management_protocol_mirror(void);
void management_protocol_progress(uint16_t completed, uint16_t total);
