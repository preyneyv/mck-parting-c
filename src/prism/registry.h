#pragma once

#include <stddef.h>

#include <prism/cartridge.h>
#include <prism/cartridge_identity.h>

typedef enum
{
  PRISM_REGISTRY_POLICY_NONE = 0,
  PRISM_REGISTRY_POLICY_BUNDLED = (1u << 0),
  PRISM_REGISTRY_POLICY_UNDELETABLE = (1u << 1),
  PRISM_REGISTRY_POLICY_HIDDEN = (1u << 2),
} prism_registry_policy_t;

typedef struct
{
  const prism_cartridge_t *cartridge;
  uint32_t policy;
} prism_registry_entry_t;

size_t prism_registry_count(void);
const prism_registry_entry_t *prism_registry_get(size_t index);
const prism_registry_entry_t *prism_registry_find(const char *id);
const prism_registry_entry_t *prism_registry_find_app_key(
    const uint8_t app_key[PRISM_APP_KEY_BYTES]);
size_t prism_registry_visible_count(void);
const prism_registry_entry_t *prism_registry_visible_get(size_t index);
