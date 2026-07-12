#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <prism/cartridge.h>

typedef struct
{
  void *allocation;
  void *got_base;
  void *backend;
} platform_cartridge_execution_t;

size_t platform_cartridge_installed_count(void);
const prism_cartridge_t *platform_cartridge_installed_get(size_t index);

/* Built-in cartridges return an empty execution object. Installed PIC
 * cartridges receive a private RAM GOT populated from their package. */
bool platform_cartridge_prepare(const prism_cartridge_t *cartridge,
                                platform_cartridge_execution_t *execution);
void platform_cartridge_release(platform_cartridge_execution_t *execution);
void platform_cartridge_call(const platform_cartridge_execution_t *execution,
                             prism_lifecycle_fn function, prism_t *context);
