#include "asset_pack.h"

#include <string.h>

static bool range_valid(uint32_t offset, uint32_t size, uint32_t limit)
{
  return offset <= limit && size <= limit - offset;
}

static bool metadata_string(const uint8_t *package, uint32_t package_size,
                            uint32_t offset, uint32_t length,
                            const char **value)
{
  if (length == 0 || offset >= package_size ||
      length >= package_size - offset ||
      package[offset + length] != '\0' ||
      memchr(package + offset, '\0', length) != NULL)
    return false;
  *value = (const char *)(package + offset);
  return true;
}

bool prism_asset_pack_parse(const void *raw_package, size_t available,
                            prism_asset_pack_view_t *view)
{
  if (raw_package == NULL || view == NULL ||
      available < sizeof(prism_asset_pack_header_t))
    return false;
  const uint8_t *package = raw_package;
  const prism_asset_pack_header_t *header = raw_package;
  uint32_t directory_bytes =
      header->file_count * sizeof(prism_asset_pack_file_entry_t);
  if (header->magic != PRISM_ASSET_PACK_MAGIC ||
      header->format_version != PRISM_ASSET_PACK_FORMAT_VERSION ||
      header->header_size != sizeof(*header) ||
      header->package_size < sizeof(*header) ||
      header->package_size > available || header->version == 0 ||
      header->file_count == 0 ||
      header->file_count > PRISM_ASSET_PACK_MAX_FILES ||
      header->directory_offset != sizeof(*header) ||
      !range_valid(header->directory_offset, directory_bytes,
                   header->package_size) ||
      header->string_table_offset !=
          header->directory_offset + directory_bytes ||
      !range_valid(header->string_table_offset, header->string_table_size,
                   header->package_size) ||
      header->data_offset !=
          ((header->string_table_offset + header->string_table_size + 3u) &
           ~3u) ||
      header->data_offset > header->package_size ||
      (header->data_offset & 3u) != 0 ||
      header->id_length > PRISM_CARTRIDGE_ID_MAX ||
      header->target_id_length > PRISM_CARTRIDGE_ID_MAX ||
      header->name_length > PRISM_ASSET_PACK_NAME_MAX ||
      !range_valid(header->id_offset, header->id_length + 1u,
                   header->string_table_offset +
                       header->string_table_size) ||
      header->id_offset < header->string_table_offset ||
      !range_valid(header->name_offset, header->name_length + 1u,
                   header->string_table_offset +
                       header->string_table_size) ||
      header->name_offset < header->string_table_offset ||
      !range_valid(header->target_id_offset,
                   header->target_id_length + 1u,
                   header->string_table_offset +
                       header->string_table_size) ||
      header->target_id_offset < header->string_table_offset ||
      (header->target_max_version != 0 &&
       header->target_max_version < header->target_min_version))
    return false;

  const char *id;
  const char *name;
  const char *target_id;
  if (!metadata_string(package, header->package_size, header->id_offset,
                       header->id_length, &id) ||
      !metadata_string(package, header->package_size, header->name_offset,
                       header->name_length, &name) ||
      !metadata_string(package, header->package_size,
                       header->target_id_offset, header->target_id_length,
                       &target_id) ||
      !prism_cartridge_id_valid_n(id, header->id_length) ||
      !prism_cartridge_id_valid_n(target_id, header->target_id_length))
    return false;
  prism_pack_key_t pack_key;
  prism_app_key_t target_key;
  if (!prism_pack_key_derive_n(id, header->id_length, pack_key) ||
      !prism_app_key_derive_n(target_id, header->target_id_length,
                              target_key) ||
      memcmp(pack_key, header->pack_key, sizeof(pack_key)) != 0 ||
      memcmp(target_key, header->target_app_key, sizeof(target_key)) != 0)
    return false;

  const prism_asset_pack_file_entry_t *entries =
      (const void *)(package + header->directory_offset);
  const char *previous_path = NULL;
  uint16_t previous_length = 0;
  uint32_t expected_data_offset = header->data_offset;
  for (uint32_t i = 0; i < header->file_count; ++i)
  {
    const prism_asset_pack_file_entry_t *entry = &entries[i];
    if (entry->flags != 0 || entry->path_length == 0 ||
        !range_valid(entry->path_offset, entry->path_length + 1u,
                     header->package_size) ||
        entry->path_offset < header->string_table_offset ||
        entry->path_offset + entry->path_length + 1u >
            header->string_table_offset + header->string_table_size ||
        package[entry->path_offset + entry->path_length] != '\0' ||
        !prism_asset_path_valid_n((const char *)package + entry->path_offset,
                                  entry->path_length) ||
        !range_valid(entry->data_offset, entry->data_length,
                     header->package_size) ||
        entry->data_offset != expected_data_offset ||
        (entry->data_offset & 3u) != 0)
      return false;
    const char *path = (const char *)package + entry->path_offset;
    if (previous_path != NULL)
    {
      size_t common = previous_length < entry->path_length
                          ? previous_length
                          : entry->path_length;
      int order = memcmp(previous_path, path, common);
      if (order > 0 ||
          (order == 0 && previous_length >= entry->path_length))
        return false;
    }
    previous_path = path;
    previous_length = entry->path_length;
    expected_data_offset = entry->data_offset + entry->data_length;
    if (i + 1u < header->file_count)
      expected_data_offset = (expected_data_offset + 3u) & ~3u;
  }
  if (expected_data_offset != header->package_size)
    return false;

  *view = (prism_asset_pack_view_t){
      .package = package,
      .header = header,
      .id = id,
      .name = name,
      .target_id = target_id,
  };
  return true;
}

bool prism_asset_pack_file_at(const prism_asset_pack_view_t *view,
                              uint32_t index, prism_asset_file_t *file)
{
  if (view == NULL || view->header == NULL || file == NULL ||
      index >= view->header->file_count)
    return false;
  const prism_asset_pack_file_entry_t *entries =
      (const void *)(view->package + view->header->directory_offset);
  const prism_asset_pack_file_entry_t *entry = &entries[index];
  *file = (prism_asset_file_t){
      .path = (const char *)view->package + entry->path_offset,
      .data = view->package + entry->data_offset,
      .size = entry->data_length,
  };
  return true;
}
