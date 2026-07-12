#pragma once

#include <stdbool.h>
#include <stdint.h>

bool prism_flash_erase(uint32_t offset, uint32_t size);
bool prism_flash_program(uint32_t offset, const uint8_t *data, uint32_t size);
