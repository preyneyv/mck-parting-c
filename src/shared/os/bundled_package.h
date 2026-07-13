#ifndef PRISM_SHARED_OS_BUNDLED_PACKAGE_H
#define PRISM_SHARED_OS_BUNDLED_PACKAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Decide whether a bundled firmware package should replace an installed
 * package with the same cartridge ID.  Equal-version packages are replaced
 * only when their packaged bytes changed, which supports development
 * reflashes without writing the cartridge flash on every boot. */
static inline bool prism_bundled_package_should_replace(
    uint32_t installed_version, uint32_t installed_bytes,
    uint32_t installed_crc32, uint32_t bundled_version,
    uint32_t bundled_bytes, uint32_t bundled_crc32)
{
  if (bundled_version != installed_version)
    return bundled_version > installed_version;
  return bundled_bytes != installed_bytes ||
         bundled_crc32 != installed_crc32;
}

#endif
