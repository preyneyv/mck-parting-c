#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/management_protocol.h>

void prism_management_cartridge_list_init(void *payload, size_t capacity,
                                          uint16_t total_count,
                                          uint16_t start_index);
bool prism_management_cartridge_list_append(
    void *payload, size_t capacity,
    const prism_management_cartridge_entry_t *entry, const char *id,
    const char *name);
size_t prism_management_cartridge_list_size(const void *payload);

