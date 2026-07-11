#include <platform/cartridge.h>

#include <stdlib.h>

size_t platform_cartridge_installed_count(void) { return 0; }

const prism_cartridge_t *platform_cartridge_installed_get(size_t index)
{
  (void)index;
  return NULL;
}

bool platform_cartridge_prepare(const prism_cartridge_t *cartridge,
                                platform_cartridge_execution_t *execution)
{
  if (cartridge == NULL || execution == NULL)
    return false;
  execution->allocation = NULL;
  execution->got_base = NULL;
  return true;
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
  (void)execution;
  if (function != NULL)
    function(context);
}
