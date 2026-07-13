#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <prism/asset_pack.h>

typedef struct
{
  const uint8_t *package;
  const prism_asset_pack_header_t *header;
  const char *id;
  const char *name;
  const char *target_id;
} prism_asset_pack_view_t;

bool prism_asset_pack_parse(const void *package, size_t available,
                            prism_asset_pack_view_t *view);
bool prism_asset_pack_file_at(const prism_asset_pack_view_t *view,
                              uint32_t index, prism_asset_file_t *file);
