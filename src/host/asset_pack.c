#include "asset_pack.h"

#include <platform/asset_pack.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shared/os/asset_pack.h>

enum { HOST_BUNDLED_PACK_COUNT = 2 };

typedef struct
{
  uint8_t *bytes;
  size_t size;
  prism_asset_pack_view_t view;
} host_pack_t;

static host_pack_t packs[HOST_BUNDLED_PACK_COUNT];
static size_t pack_count;

static const char *const pack_paths[HOST_BUNDLED_PACK_COUNT] = {
    PRISM_HOST_GOLDEN_PACK_PATH,
    PRISM_HOST_NEVER_GONNA_PACK_PATH,
};

static void pack_unload(host_pack_t *pack)
{
  if (pack == NULL)
    return;
  free(pack->bytes);
  memset(pack, 0, sizeof(*pack));
}

static bool pack_load(host_pack_t *pack, const char *path)
{
  FILE *file = fopen(path, "rb");
  if (file == NULL || fseek(file, 0, SEEK_END) != 0)
    goto fail;
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0)
    goto fail;
  pack->bytes = malloc((size_t)length);
  if (pack->bytes == NULL ||
      fread(pack->bytes, 1, (size_t)length, file) != (size_t)length)
    goto fail;
  fclose(file);
  file = NULL;
  pack->size = (size_t)length;
  if (!prism_asset_pack_parse(pack->bytes, pack->size, &pack->view))
    goto fail;
  return true;

fail:
  if (file != NULL)
    fclose(file);
  fprintf(stderr, "failed to load asset pack: %s\n", path);
  pack_unload(pack);
  return false;
}

bool host_asset_pack_init_bundled(void)
{
  for (size_t i = 0; i < pack_count; ++i)
    pack_unload(&packs[i]);
  pack_count = 0;
  for (size_t i = 0; i < HOST_BUNDLED_PACK_COUNT; ++i)
  {
    if (!pack_load(&packs[i], pack_paths[i]))
    {
      for (size_t j = 0; j < pack_count; ++j)
        pack_unload(&packs[j]);
      pack_count = 0;
      return false;
    }
    ++pack_count;
  }
  return true;
}

static bool targets(const host_pack_t *pack,
                    const prism_cartridge_t *cartridge)
{
  return pack != NULL && cartridge != NULL &&
         strcmp(pack->view.target_id, cartridge->id) == 0 &&
         cartridge->version >= pack->view.header->target_min_version &&
         (pack->view.header->target_max_version == 0 ||
          cartridge->version <= pack->view.header->target_max_version);
}

size_t platform_asset_pack_count(const prism_cartridge_t *cartridge)
{
  size_t count = 0;
  for (size_t i = 0; i < pack_count; ++i)
    if (targets(&packs[i], cartridge))
      ++count;
  return count;
}

bool platform_asset_pack_get(const prism_cartridge_t *cartridge, size_t index,
                             prism_asset_pack_info_t *info)
{
  if (info == NULL)
    return false;
  for (size_t i = 0; i < pack_count; ++i)
  {
    if (!targets(&packs[i], cartridge))
      continue;
    if (index-- != 0)
      continue;
    *info = (prism_asset_pack_info_t){
        .handle = (prism_asset_pack_handle_t)i + 1u,
        .version = packs[i].view.header->version,
        .file_count = packs[i].view.header->file_count,
        .id = packs[i].view.id,
        .name = packs[i].view.name,
    };
    return true;
  }
  return false;
}

bool platform_asset_file_get(const prism_cartridge_t *cartridge,
                             prism_asset_pack_handle_t handle, uint32_t index,
                             prism_asset_file_t *file)
{
  if (handle == 0 || handle > pack_count)
    return false;
  const host_pack_t *pack = &packs[handle - 1u];
  return targets(pack, cartridge) &&
         prism_asset_pack_file_at(&pack->view, index, file);
}
