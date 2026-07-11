#include <prism/cartridges.h>
#include <prism/registry.h>
#include <platform/cartridge.h>

#include <string.h>

#define BUILTIN_POLICY                                                        \
  (PRISM_REGISTRY_POLICY_BUNDLED | PRISM_REGISTRY_POLICY_UNDELETABLE)

static const prism_registry_entry_t entries[] = {
#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
    {&cartridge_bongocat, BUILTIN_POLICY},
    {&cartridge_morse, BUILTIN_POLICY},
    {&cartridge_asteroids, BUILTIN_POLICY},
    {&cartridge_beatline, BUILTIN_POLICY},
#endif
    {&cartridge_dummy, BUILTIN_POLICY | PRISM_REGISTRY_POLICY_HIDDEN},
    {&cartridge_full_test, BUILTIN_POLICY | PRISM_REGISTRY_POLICY_HIDDEN},
};

static size_t builtin_count(void)
{
  return sizeof(entries) / sizeof(entries[0]);
}

size_t prism_registry_count(void)
{
  return builtin_count() + platform_cartridge_installed_count();
}

const prism_registry_entry_t *prism_registry_get(size_t index)
{
  if (index < builtin_count())
    return &entries[index];
  const prism_cartridge_t *cartridge =
      platform_cartridge_installed_get(index - builtin_count());
  if (cartridge == NULL)
    return NULL;
  static prism_registry_entry_t installed[32];
  size_t slot = (index - builtin_count()) % 32;
  installed[slot].cartridge = cartridge;
  installed[slot].policy = PRISM_REGISTRY_POLICY_NONE;
  return &installed[slot];
}

const prism_registry_entry_t *prism_registry_find(const char *slug)
{
  if (slug == NULL)
    return NULL;
  for (size_t i = 0; i < prism_registry_count(); ++i)
  {
    const prism_registry_entry_t *entry = prism_registry_get(i);
    if (entry != NULL && strcmp(entry->cartridge->slug, slug) == 0)
      return entry;
  }
  return NULL;
}

const prism_registry_entry_t *prism_registry_find_app_id(uint32_t app_id)
{
  for (size_t i = 0; i < prism_registry_count(); ++i)
  {
    const prism_registry_entry_t *entry = prism_registry_get(i);
    if (entry != NULL && entry->cartridge->app_id == app_id)
      return entry;
  }
  return NULL;
}

size_t prism_registry_visible_count(void)
{
  size_t count = 0;
  for (size_t i = 0; i < prism_registry_count(); ++i)
  {
    const prism_registry_entry_t *entry = prism_registry_get(i);
    if (entry != NULL &&
        (entry->policy & PRISM_REGISTRY_POLICY_HIDDEN) == 0)
      ++count;
  }
  return count;
}

const prism_registry_entry_t *prism_registry_visible_get(size_t index)
{
  for (size_t i = 0; i < prism_registry_count(); ++i)
  {
    const prism_registry_entry_t *entry = prism_registry_get(i);
    if (entry == NULL)
      continue;
    if ((entry->policy & PRISM_REGISTRY_POLICY_HIDDEN) != 0)
      continue;
    if (index-- == 0)
      return entry;
  }
  return NULL;
}
