#include <platform/cartridge.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "cartridge_vm.h"

static host_cartridge_package_t loaded;
static bool loaded_valid;

bool host_cartridge_load(const char *path)
{
  host_cartridge_package_unload(&loaded);
  loaded_valid = host_cartridge_package_load(&loaded, path);
  return loaded_valid;
}

size_t platform_cartridge_installed_count(void)
{
  return loaded_valid ? 1u : 0u;
}

const prism_cartridge_t *platform_cartridge_installed_get(size_t index)
{
  return loaded_valid && index == 0 ? &loaded.descriptor : NULL;
}

bool platform_cartridge_prepare(const prism_cartridge_t *cartridge,
                                platform_cartridge_execution_t *execution)
{
  if (cartridge == NULL || execution == NULL)
    return false;
  memset(execution, 0, sizeof(*execution));
  if (!loaded_valid || cartridge != &loaded.descriptor)
    return true;
  execution->backend = host_cartridge_vm_create(&loaded);
  return execution->backend != NULL;
}

void platform_cartridge_release(platform_cartridge_execution_t *execution)
{
  if (execution == NULL)
    return;
  if (execution->backend != NULL)
    host_cartridge_vm_destroy(execution->backend);
  free(execution->allocation);
  memset(execution, 0, sizeof(*execution));
}

void platform_cartridge_call(const platform_cartridge_execution_t *execution,
                             prism_lifecycle_fn function, prism_t *context)
{
  if (function == NULL)
    return;
  if (execution == NULL || execution->backend == NULL)
  {
    function(context);
    return;
  }

  if (!host_cartridge_vm_call(execution->backend,
                              (uint32_t)(uintptr_t)function, context))
    fprintf(stderr, "cartridge lifecycle call failed at 0x%08lx\n",
            (unsigned long)(uintptr_t)function);
}
