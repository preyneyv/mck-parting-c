#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <prism/package.h>

typedef uint32_t (*prism_package_import_resolver_t)(uint16_t symbol,
                                                    void *user);

bool prism_package_prepare_launch_image(
    const uint8_t *package, const prism_package_header_t *header,
    uint32_t image_base, uint8_t *got, uint32_t rw_base, uint8_t *rw,
    prism_package_import_resolver_t resolve_import, void *user);
