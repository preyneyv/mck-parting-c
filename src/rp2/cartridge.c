#include <platform/cartridge.h>

#include <stdlib.h>

#include <hardware/structs/watchdog.h>

#include "cartridge_storage.h"

void prism_call_with_got(prism_lifecycle_fn function, void *got_base,
                         prism_t *context);

size_t platform_cartridge_installed_count(void)
{
  return cartridge_storage_installed_count();
}

const prism_cartridge_t *platform_cartridge_installed_get(size_t index)
{
  return cartridge_storage_installed_get(index);
}

bool platform_cartridge_prepare(const prism_cartridge_t *cartridge,
                                platform_cartridge_execution_t *execution)
{
  if (cartridge == NULL || execution == NULL)
    return false;
  execution->allocation = NULL;
  execution->got_base = NULL;
  if (!cartridge_storage_owns(cartridge))
    return true; /* Firmware-bundled descriptor. */
  watchdog_hw->scratch[0] = 0x50524550u; /* PREP */
  watchdog_hw->scratch[1] = (uint32_t)(uintptr_t)cartridge;
  bool prepared = cartridge_storage_prepare(cartridge, execution);
  watchdog_hw->scratch[0] = 0;
  watchdog_hw->scratch[1] = 0;
  return prepared;
}

void platform_cartridge_release(platform_cartridge_execution_t *execution)
{
  if (execution == NULL)
    return;
  free(execution->allocation);
  execution->allocation = NULL;
  execution->got_base = NULL;
}

void platform_cartridge_call(const platform_cartridge_execution_t *execution,
                             prism_lifecycle_fn function, prism_t *context)
{
  if (function == NULL)
    return;
  if (execution != NULL && execution->got_base != NULL)
  {
    watchdog_hw->scratch[0] = 0x43414c4cu; /* CALL */
    watchdog_hw->scratch[1] = (uint32_t)(uintptr_t)function;
    prism_call_with_got(function, execution->got_base, context);
    watchdog_hw->scratch[0] = 0;
    watchdog_hw->scratch[1] = 0;
  }
  else
    function(context);
}
