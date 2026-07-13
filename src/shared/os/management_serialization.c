#include <prism/management_serialization.h>

#include <limits.h>
#include <string.h>

void prism_management_cartridge_list_init(void *payload, size_t capacity,
                                          uint16_t total_count,
                                          uint16_t start_index)
{
  if (payload == NULL || capacity < sizeof(prism_management_cartridge_list_t))
    return;
  prism_management_cartridge_list_t *list = payload;
  *list = (prism_management_cartridge_list_t){
      .total_count = total_count,
      .start_index = start_index,
  };
}

size_t prism_management_cartridge_list_size(const void *payload)
{
  if (payload == NULL)
    return 0;
  const prism_management_cartridge_list_t *list = payload;
  return sizeof(*list) +
         (size_t)list->count * sizeof(prism_management_cartridge_entry_t) +
         list->string_bytes;
}

bool prism_management_cartridge_list_append(
    void *payload, size_t capacity,
    const prism_management_cartridge_entry_t *entry, const char *id,
    const char *name)
{
  if (payload == NULL || entry == NULL || id == NULL || name == NULL)
    return false;
  prism_management_cartridge_list_t *list = payload;
  size_t id_length = strlen(id);
  size_t name_length = strlen(name);
  if (id_length > UINT16_MAX || name_length > UINT16_MAX ||
      id_length + name_length >
          (size_t)UINT16_MAX - list->string_bytes)
    return false;
  size_t old_entries_bytes =
      (size_t)list->count * sizeof(prism_management_cartridge_entry_t);
  size_t old_size = sizeof(*list) + old_entries_bytes + list->string_bytes;
  size_t added = sizeof(prism_management_cartridge_entry_t) + id_length +
                 name_length;
  if (old_size > capacity || added > capacity - old_size)
    return false;

  uint8_t *bytes = payload;
  uint8_t *old_strings = bytes + sizeof(*list) + old_entries_bytes;
  memmove(old_strings + sizeof(prism_management_cartridge_entry_t),
          old_strings, list->string_bytes);
  prism_management_cartridge_entry_t serialized = *entry;
  serialized.id_offset = list->string_bytes;
  serialized.id_length = (uint16_t)id_length;
  serialized.name_offset = (uint16_t)(list->string_bytes + id_length);
  serialized.name_length = (uint16_t)name_length;
  memcpy(old_strings, &serialized, sizeof(serialized));

  uint8_t *strings = old_strings + sizeof(serialized) + list->string_bytes;
  memcpy(strings, id, id_length);
  memcpy(strings + id_length, name, name_length);
  ++list->count;
  list->string_bytes =
      (uint16_t)(list->string_bytes + id_length + name_length);
  return true;
}

void prism_management_asset_pack_list_init(void *payload, size_t capacity,
                                           uint16_t total_count,
                                           uint16_t start_index)
{
  prism_management_cartridge_list_init(payload, capacity, total_count,
                                        start_index);
}

size_t prism_management_asset_pack_list_size(const void *payload)
{
  if (payload == NULL)
    return 0;
  const prism_management_cartridge_list_t *list = payload;
  return sizeof(*list) +
         (size_t)list->count * sizeof(prism_management_asset_pack_entry_t) +
         list->string_bytes;
}

bool prism_management_asset_pack_list_append(
    void *payload, size_t capacity,
    const prism_management_asset_pack_entry_t *entry, const char *id,
    const char *name, const char *target_id)
{
  if (payload == NULL || entry == NULL || id == NULL || name == NULL ||
      target_id == NULL)
    return false;
  prism_management_cartridge_list_t *list = payload;
  size_t id_length = strlen(id);
  size_t name_length = strlen(name);
  size_t target_length = strlen(target_id);
  size_t string_added = id_length + name_length + target_length;
  if (id_length > UINT16_MAX || name_length > UINT16_MAX ||
      target_length > UINT16_MAX ||
      string_added > (size_t)UINT16_MAX - list->string_bytes)
    return false;
  size_t old_entries_bytes =
      (size_t)list->count * sizeof(prism_management_asset_pack_entry_t);
  size_t old_size = sizeof(*list) + old_entries_bytes + list->string_bytes;
  size_t added = sizeof(prism_management_asset_pack_entry_t) + string_added;
  if (old_size > capacity || added > capacity - old_size)
    return false;

  uint8_t *bytes = payload;
  uint8_t *old_strings = bytes + sizeof(*list) + old_entries_bytes;
  memmove(old_strings + sizeof(prism_management_asset_pack_entry_t),
          old_strings, list->string_bytes);
  prism_management_asset_pack_entry_t serialized = *entry;
  serialized.id_offset = list->string_bytes;
  serialized.id_length = (uint16_t)id_length;
  serialized.name_offset = (uint16_t)(list->string_bytes + id_length);
  serialized.name_length = (uint16_t)name_length;
  serialized.target_id_offset =
      (uint16_t)(list->string_bytes + id_length + name_length);
  serialized.target_id_length = (uint16_t)target_length;
  memcpy(old_strings, &serialized, sizeof(serialized));

  uint8_t *strings = old_strings + sizeof(serialized) + list->string_bytes;
  memcpy(strings, id, id_length);
  memcpy(strings + id_length, name, name_length);
  memcpy(strings + id_length + name_length, target_id, target_length);
  ++list->count;
  list->string_bytes = (uint16_t)(list->string_bytes + string_added);
  return true;
}
