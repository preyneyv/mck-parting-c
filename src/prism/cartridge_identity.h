#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PRISM_APP_KEY_BYTES 16u
#define PRISM_CARTRIDGE_ID_MAX 253u

typedef uint8_t prism_app_key_t[PRISM_APP_KEY_BYTES];

typedef enum
{
  PRISM_CARTRIDGE_UPDATE_SEPARATE,
  PRISM_CARTRIDGE_UPDATE_MATCH,
  PRISM_CARTRIDGE_UPDATE_DOWNGRADE,
  PRISM_CARTRIDGE_UPDATE_KEY_COLLISION,
} prism_cartridge_update_result_t;

bool prism_cartridge_id_valid_n(const char *id, size_t length);
bool prism_cartridge_id_valid(const char *id);
bool prism_app_key_derive_n(const char *id, size_t length,
                            prism_app_key_t app_key);
bool prism_app_key_derive(const char *id, prism_app_key_t app_key);
prism_cartridge_update_result_t prism_cartridge_update_check(
    const uint8_t installed_key[PRISM_APP_KEY_BYTES],
    const char *installed_id, uint32_t installed_version,
    const uint8_t candidate_key[PRISM_APP_KEY_BYTES],
    const char *candidate_id, uint32_t candidate_version);
