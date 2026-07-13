#include "cartridge_image.h"

#include <stddef.h>
#include <string.h>

bool prism_package_prepare_launch_image(
    const uint8_t *package, const prism_package_header_t *header,
    uint32_t image_base, uint8_t *got, uint32_t rw_base, uint8_t *rw,
    prism_package_import_resolver_t resolve_import, void *user)
{
  if (package == NULL || header == NULL ||
      (header->got_size != 0 && got == NULL) ||
      (header->rw_size != 0 && rw == NULL))
    return false;

  if (header->got_size != 0)
    memcpy(got, package + header->got_offset, header->got_size);
  if (header->rw_size != 0)
  {
    memcpy(rw, package + header->rw_offset, header->rw_init_size);
    memset(rw + header->rw_init_size, 0,
           header->rw_size - header->rw_init_size);
  }

  const prism_package_relocation_t *relocations =
      (const void *)(package + header->relocations_offset);
  uint32_t got_end = header->got_offset + header->got_size;
  uint32_t rw_init_end = header->rw_offset + header->rw_init_size;
  for (uint32_t i = 0; i < header->relocation_count; ++i)
  {
    uint32_t patch = relocations[i].patch_offset;
    uint32_t *word;
    if (patch >= header->got_offset && patch < got_end)
      word = (void *)(got + patch - header->got_offset);
    else if (patch >= header->rw_offset && patch < rw_init_end)
      word = (void *)(rw + patch - header->rw_offset);
    else
      continue;

    uint32_t linked = *word & ~1u;
    uint32_t thumb = *word & 1u;
    if (linked >= header->image_size)
    {
      uint32_t relative = linked - header->image_size;
      if (relative >= header->rw_size)
        return false;
      *word = (rw_base + relative) | thumb;
    }
    else
      *word = (image_base + linked) | thumb;
  }

  const prism_package_import_t *imports =
      (const void *)(package + header->imports_offset);
  for (uint32_t i = 0; i < header->import_count; ++i)
  {
    if (resolve_import == NULL)
      return false;
    uint32_t address = resolve_import(imports[i].symbol, user);
    if (address == 0)
      return false;
    uint32_t *word =
        (void *)(got + imports[i].patch_offset - header->got_offset);
    *word = address;
  }

  return true;
}
