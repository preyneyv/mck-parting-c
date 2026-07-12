#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/cartridge.h>
#include <prism/package.h>

typedef struct host_cartridge_package
{
  uint8_t *bytes;
  size_t size;
  prism_package_header_t header;
  prism_cartridge_t descriptor;
  uint32_t guest_descriptor;
} host_cartridge_package_t;

typedef struct host_cartridge_vm host_cartridge_vm_t;

bool host_cartridge_package_load(host_cartridge_package_t *package,
                                 const char *path);
void host_cartridge_package_unload(host_cartridge_package_t *package);
host_cartridge_vm_t *
host_cartridge_vm_create(const host_cartridge_package_t *package);
void host_cartridge_vm_destroy(host_cartridge_vm_t *vm);
bool host_cartridge_vm_call(host_cartridge_vm_t *vm, uint32_t function,
                            prism_t *context);
