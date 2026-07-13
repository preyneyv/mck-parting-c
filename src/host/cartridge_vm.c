#include "cartridge_vm.h"
#include "cartridge_abi.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <platform/display.h>
#include <platform/time.h>
#include <prism/cartridge_identity.h>
#include <qrcodegen.h>
#include <shared/audio/synth_internal.h>
#include <shared/leaderboard/leaderboard.h>
#include <shared/os/cartridge_image.h>
#include <u8g2.h>
#include <unicorn/arm.h>
#include <unicorn/unicorn.h>

enum
{
  GUEST_IMAGE_BASE = 0x10000000u,
  GUEST_RAM_BASE = 0x20000000u,
  GUEST_RAM_BYTES = 16u * 1024u * 1024u,
  GUEST_API_ADDRESS = GUEST_RAM_BASE,
  GUEST_CONTEXT_ADDRESS = GUEST_RAM_BASE + 0x100u,
  GUEST_PERSISTENT_ADDRESS = GUEST_RAM_BASE + 0x1000u,
  GUEST_STACK_ADDRESS = GUEST_RAM_BASE + GUEST_RAM_BYTES - 0x10000u,
  GUEST_STACK_BYTES = 0x10000u,
  GUEST_STOP_ADDRESS = 0x0fff0000u,
  /* Cortex-M treats 0xe0000000+ as its system region and raises an exception
   * before a code hook can intercept it. Keep synthetic call gates in the
   * ordinary code region instead. */
  GUEST_TRAP_BASE = 0x11000000u,
  GUEST_TRAP_BYTES = 0x10000u,
  GUEST_OBJECT_BASE = 0x30000000u,
  GUEST_DISPLAY_HANDLE = GUEST_OBJECT_BASE + 0x10000u,
  GUEST_SYNTH_HANDLE = GUEST_OBJECT_BASE + 0x10004u,
  GUEST_API_TRAP_BASE = 0x100u,
  GUEST_ALLOCATION_MAX = 256u,
  GUEST_ANIMATION_MAX = 32u,
};

typedef struct
{
  uint32_t address;
  uint32_t size;
  bool used;
} guest_allocation_t;

typedef struct
{
  uint32_t subject;
  int32_t start;
  int32_t end;
  uint64_t started_at;
  uint32_t duration_ms;
  uint8_t easing;
  bool active;
} guest_animation_t;

struct host_cartridge_vm
{
  uc_engine *uc;
  const host_cartridge_package_t *package;
  prism_t *context;
  uint32_t persistent_address;
  uint32_t heap_next;
  uint32_t heap_limit;
  guest_allocation_t allocations[GUEST_ALLOCATION_MAX];
  guest_animation_t animations[GUEST_ANIMATION_MAX];
  bool failed;
  bool failure_pending;
  bool unsupported_reported[PRISM_PACKAGE_MAX_IMPORTS];
};

typedef struct
{
  uint32_t magic;
  uint16_t abi_version;
  uint16_t descriptor_size;
  uint32_t tick_divider;
  uint32_t version;
  uint32_t id;
  uint32_t name;
  uint32_t icon;
  uint32_t enter;
  uint32_t tick;
  uint32_t frame;
  uint32_t pause;
  uint32_t resume;
  uint32_t leave;
  uint32_t persistent_size;
  uint16_t persistent_schema_version;
  uint16_t reserved;
} guest_cartridge_descriptor_t;

_Static_assert(sizeof(guest_cartridge_descriptor_t) == 60,
               "ARM cartridge descriptor layout changed");

static uint32_t align_up(uint32_t value, uint32_t alignment)
{
  return (value + alignment - 1u) & ~(alignment - 1u);
}

static bool range_valid(uint32_t offset, uint32_t size, uint32_t limit)
{
  return offset <= limit && size <= limit - offset;
}

static bool image_offset_valid(uint32_t value, uint32_t size,
                               uint32_t image_size)
{
  value &= ~1u;
  return value <= image_size && size <= image_size - value;
}

static bool import_is_object(uint16_t symbol)
{
  switch ((prism_package_import_symbol_t)symbol)
  {
#define PRISM_IMPORT_KIND_FUNCTION false
#define PRISM_IMPORT_KIND_OBJECT true
#define PRISM_IMPORT(id, name, linker, kind, resolver)                        \
  case PRISM_IMPORT_##name: return PRISM_IMPORT_KIND_##kind;
#include <prism/imports.def>
#undef PRISM_IMPORT
#undef PRISM_IMPORT_KIND_OBJECT
#undef PRISM_IMPORT_KIND_FUNCTION
  default: return false;
  }
}

static bool import_known(uint16_t symbol)
{
  switch ((prism_package_import_symbol_t)symbol)
  {
#define PRISM_IMPORT(id, name, linker, kind, resolver)                        \
  case PRISM_IMPORT_##name: return true;
#include <prism/imports.def>
#undef PRISM_IMPORT
  default: return false;
  }
}

static bool package_validate(const uint8_t *bytes, size_t size,
                             prism_package_header_t *header,
                             guest_cartridge_descriptor_t *descriptor)
{
  if (bytes == NULL || size < sizeof(*header) || size > UINT32_MAX)
    return false;
  memcpy(header, bytes, sizeof(*header));
  if (header->magic != PRISM_PACKAGE_MAGIC ||
      header->format_version != PRISM_PACKAGE_FORMAT_VERSION ||
      header->header_size != sizeof(*header) ||
      header->cartridge_abi != PRISM_CARTRIDGE_ABI_VERSION ||
      header->tick_divider == 0 ||
      header->package_size != size ||
      header->u8g2_abi_hash != PRISM_U8G2_ABI_HASH ||
      header->relocation_count > PRISM_PACKAGE_MAX_RELOCATIONS ||
      header->import_count > PRISM_PACKAGE_MAX_IMPORTS ||
      header->rw_size > PRISM_PACKAGE_MAX_RW_BYTES)
    return false;

  if (!range_valid(header->image_offset, header->image_size, size) ||
      !range_valid(header->descriptor_offset, sizeof(*descriptor), size) ||
      !range_valid(header->got_offset, header->got_size, size) ||
      !range_valid(header->relocations_offset,
                   header->relocation_count *
                       sizeof(prism_package_relocation_t),
                   size) ||
      !range_valid(header->imports_offset,
                   header->import_count * sizeof(prism_package_import_t),
                   size) ||
      !range_valid(header->rw_offset, header->rw_init_size, size) ||
      header->rw_init_size > header->rw_size ||
      header->descriptor_offset < header->image_offset ||
      header->descriptor_offset + sizeof(*descriptor) >
          header->image_offset + header->image_size ||
      header->got_offset < header->image_offset ||
      header->got_offset + header->got_size >
          header->image_offset + header->image_size ||
      (header->got_size & 3u) != 0 ||
      (header->got_base_offset & 3u) != 0 ||
      (header->got_size == 0 ? header->got_base_offset != 0
                             : header->got_base_offset >= header->got_size))
    return false;

  memcpy(descriptor, bytes + header->descriptor_offset, sizeof(*descriptor));
  if (descriptor->magic != PRISM_CARTRIDGE_MAGIC ||
      descriptor->abi_version != PRISM_CARTRIDGE_ABI_VERSION ||
      descriptor->descriptor_size != sizeof(*descriptor) ||
      descriptor->tick_divider != header->tick_divider ||
      descriptor->persistent_size != header->persistent_size ||
      descriptor->persistent_schema_version != header->persistent_schema ||
      !image_offset_valid(descriptor->id, 1, header->image_size) ||
      !image_offset_valid(descriptor->name, 1, header->image_size) ||
      !image_offset_valid(descriptor->icon, PRISM_CARTRIDGE_ICON_BYTES,
                          header->image_size) ||
      !image_offset_valid(descriptor->frame, 2, header->image_size))
    return false;

  const char *id =
      (const char *)(bytes + header->image_offset + descriptor->id);
  const char *name =
      (const char *)(bytes + header->image_offset + descriptor->name);
  const char *id_end = memchr(id, '\0', header->image_size - descriptor->id);
  prism_app_key_t derived_key;
  if (id_end == NULL ||
      memchr(name, '\0', header->image_size - descriptor->name) == NULL ||
      !prism_app_key_derive_n(id, (size_t)(id_end - id), derived_key) ||
      memcmp(derived_key, header->app_key, sizeof(derived_key)) != 0)
    return false;

  const prism_package_relocation_t *relocations =
      (const void *)(bytes + header->relocations_offset);
  uint32_t descriptor_end = header->descriptor_offset + sizeof(*descriptor);
  uint32_t got_end = header->got_offset + header->got_size;
  uint32_t rw_end = header->rw_offset + header->rw_init_size;
  for (uint32_t i = 0; i < header->relocation_count; ++i)
  {
    uint32_t patch = relocations[i].patch_offset;
    if ((patch & 3u) != 0 || !range_valid(patch, 4, size) ||
        !((patch >= header->descriptor_offset && patch < descriptor_end) ||
          (patch >= header->got_offset && patch < got_end) ||
          (patch >= header->rw_offset && patch < rw_end)))
      return false;
    uint32_t value;
    memcpy(&value, bytes + patch, sizeof(value));
    if (!image_offset_valid(value, 0,
                            header->image_size + header->rw_size))
      return false;
  }

  const prism_package_import_t *imports =
      (const void *)(bytes + header->imports_offset);
  for (uint32_t i = 0; i < header->import_count; ++i)
    if (imports[i].reserved != 0 || !import_known(imports[i].symbol) ||
        (imports[i].patch_offset & 3u) != 0 ||
        imports[i].patch_offset < header->got_offset ||
        !range_valid(imports[i].patch_offset, 4, got_end))
      return false;
  return true;
}

bool host_cartridge_package_load(host_cartridge_package_t *package,
                                 const char *path)
{
  if (package == NULL || path == NULL)
    return false;
  memset(package, 0, sizeof(*package));
  FILE *file = fopen(path, "rb");
  if (file == NULL)
  {
    fprintf(stderr, "cannot open cartridge '%s': %s\n", path,
            strerror(errno));
    return false;
  }
  if (fseek(file, 0, SEEK_END) != 0)
  {
    fclose(file);
    return false;
  }
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0)
  {
    fclose(file);
    return false;
  }
  package->size = (size_t)length;
  package->bytes = malloc(package->size);
  if (package->bytes == NULL ||
      fread(package->bytes, 1, package->size, file) != package->size)
  {
    fprintf(stderr, "cannot read cartridge '%s'\n", path);
    fclose(file);
    host_cartridge_package_unload(package);
    return false;
  }
  fclose(file);

  guest_cartridge_descriptor_t guest;
  if (!package_validate(package->bytes, package->size, &package->header,
                        &guest))
  {
    fprintf(stderr, "invalid or incompatible cartridge: %s\n", path);
    host_cartridge_package_unload(package);
    return false;
  }

  const uint8_t *image = package->bytes + package->header.image_offset;
  package->guest_descriptor =
      GUEST_IMAGE_BASE + package->header.descriptor_offset -
      package->header.image_offset;
  package->descriptor = (prism_cartridge_t){
      .magic = guest.magic,
      .abi_version = guest.abi_version,
      .descriptor_size = sizeof(prism_cartridge_t),
      .tick_divider = guest.tick_divider,
      .version = guest.version,
      .id = (const char *)(image + guest.id),
      .name = (const char *)(image + guest.name),
      .icon = image + guest.icon,
      .enter = (prism_lifecycle_fn)(uintptr_t)(GUEST_IMAGE_BASE + guest.enter),
      .tick = (prism_lifecycle_fn)(uintptr_t)(GUEST_IMAGE_BASE + guest.tick),
      .frame = (prism_lifecycle_fn)(uintptr_t)(GUEST_IMAGE_BASE + guest.frame),
      .pause = (prism_lifecycle_fn)(uintptr_t)(GUEST_IMAGE_BASE + guest.pause),
      .resume = (prism_lifecycle_fn)(uintptr_t)(GUEST_IMAGE_BASE + guest.resume),
      .leave = (prism_lifecycle_fn)(uintptr_t)(GUEST_IMAGE_BASE + guest.leave),
      .persistent_size = guest.persistent_size,
      .persistent_schema_version = guest.persistent_schema_version,
      .reserved = guest.reserved,
  };
  printf("loaded cartridge: %s (%lu bytes)\n", package->descriptor.name,
         (unsigned long)package->size);
  return true;
}

void host_cartridge_package_unload(host_cartridge_package_t *package)
{
  if (package == NULL)
    return;
  free(package->bytes);
  memset(package, 0, sizeof(*package));
}

static bool guest_read(host_cartridge_vm_t *vm, uint32_t address, void *data,
                       size_t size)
{
  if (size == 0)
    return true;
  uc_err error = uc_mem_read(vm->uc, address, data, size);
  if (error == UC_ERR_OK)
    return true;
  fprintf(stderr, "cartridge read at 0x%08x failed: %s\n", address,
          uc_strerror(error));
  vm->failed = true;
  return false;
}

static bool guest_write(host_cartridge_vm_t *vm, uint32_t address,
                        const void *data, size_t size)
{
  if (size == 0)
    return true;
  uc_err error = uc_mem_write(vm->uc, address, data, size);
  if (error == UC_ERR_OK)
    return true;
  fprintf(stderr, "cartridge write at 0x%08x failed: %s\n", address,
          uc_strerror(error));
  vm->failed = true;
  return false;
}

static uint32_t guest_read_u32(host_cartridge_vm_t *vm, uint32_t address)
{
  uint32_t value = 0;
  guest_read(vm, address, &value, sizeof(value));
  return value;
}

static void guest_write_u32(host_cartridge_vm_t *vm, uint32_t address,
                            uint32_t value)
{
  guest_write(vm, address, &value, sizeof(value));
}

static bool guest_string(host_cartridge_vm_t *vm, uint32_t address,
                         char *buffer, size_t capacity)
{
  if (capacity == 0)
    return false;
  for (size_t i = 0; i < capacity; ++i)
  {
    if (!guest_read(vm, address + (uint32_t)i, &buffer[i], 1))
      return false;
    if (buffer[i] == '\0')
      return true;
  }
  buffer[capacity - 1] = '\0';
  return false;
}

static uint32_t reg_read(host_cartridge_vm_t *vm, int reg)
{
  uint32_t value = 0;
  if (uc_reg_read(vm->uc, reg, &value) != UC_ERR_OK)
    vm->failed = true;
  return value;
}

static void reg_write(host_cartridge_vm_t *vm, int reg, uint32_t value)
{
  if (uc_reg_write(vm->uc, reg, &value) != UC_ERR_OK)
    vm->failed = true;
}

static uint32_t stack_arg(host_cartridge_vm_t *vm, uint32_t index)
{
  return guest_read_u32(vm, reg_read(vm, UC_ARM_REG_SP) + index * 4u);
}

static float word_float(uint32_t word)
{
  float value;
  memcpy(&value, &word, sizeof(value));
  return value;
}

static uint32_t float_word(float value)
{
  uint32_t word;
  memcpy(&word, &value, sizeof(word));
  return word;
}

static uint64_t words_u64(uint32_t low, uint32_t high)
{
  return (uint64_t)low | ((uint64_t)high << 32);
}

static void reg_write_u64(host_cartridge_vm_t *vm, int low_reg,
                          int high_reg, uint64_t value)
{
  reg_write(vm, low_reg, (uint32_t)value);
  reg_write(vm, high_reg, (uint32_t)(value >> 32));
}

static double words_double(uint32_t low, uint32_t high)
{
  uint64_t words = words_u64(low, high);
  double value;
  memcpy(&value, &words, sizeof(value));
  return value;
}

static void reg_write_double(host_cartridge_vm_t *vm, double value)
{
  uint64_t words;
  memcpy(&words, &value, sizeof(words));
  reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, words);
}

static uint32_t leading_zeroes_u64(uint64_t value, uint32_t width)
{
  uint32_t count = 0;
  uint64_t bit = UINT64_C(1) << (width - 1u);
  while (count < width && (value & bit) == 0)
  {
    ++count;
    bit >>= 1;
  }
  return count;
}

static uint32_t trailing_zeroes_u64(uint64_t value, uint32_t width)
{
  uint32_t count = 0;
  while (count < width && (value & 1u) == 0)
  {
    ++count;
    value >>= 1;
  }
  return count;
}

static uint32_t population_count_u64(uint64_t value)
{
  uint32_t count = 0;
  while (value != 0)
  {
    value &= value - 1u;
    ++count;
  }
  return count;
}

static void fp_compare_flags(host_cartridge_vm_t *vm, double left,
                             double right)
{
  enum
  {
    CPSR_N = 1u << 31,
    CPSR_Z = 1u << 30,
    CPSR_C = 1u << 29,
    CPSR_V = 1u << 28,
    CPSR_NZCV = CPSR_N | CPSR_Z | CPSR_C | CPSR_V,
  };
  uint32_t cpsr = reg_read(vm, UC_ARM_REG_CPSR) & ~CPSR_NZCV;
  if (isnan(left) || isnan(right))
    cpsr |= CPSR_C | CPSR_V;
  else if (left == right)
    cpsr |= CPSR_Z | CPSR_C;
  else if (left < right)
    cpsr |= CPSR_N;
  else
    cpsr |= CPSR_C;
  reg_write(vm, UC_ARM_REG_CPSR, cpsr);
}

static int32_t float_to_i32(double value)
{
  if (isnan(value))
    return 0;
  if (value >= INT32_MAX)
    return INT32_MAX;
  if (value <= INT32_MIN)
    return INT32_MIN;
  return (int32_t)value;
}

static uint32_t float_to_u32(double value)
{
  if (isnan(value) || value <= 0.0)
    return 0;
  if (value >= UINT32_MAX)
    return UINT32_MAX;
  return (uint32_t)value;
}

static int64_t float_to_i64(double value)
{
  if (isnan(value))
    return 0;
  if (value >= (double)INT64_MAX)
    return INT64_MAX;
  if (value <= (double)INT64_MIN)
    return INT64_MIN;
  return (int64_t)value;
}

static uint64_t float_to_u64(double value)
{
  if (isnan(value) || value <= 0.0)
    return 0;
  if (value >= (double)UINT64_MAX)
    return UINT64_MAX;
  return (uint64_t)value;
}

static const uint8_t *font_for_token(uint32_t token)
{
  uint16_t symbol = (uint16_t)((token - GUEST_OBJECT_BASE) / 4u);
  switch ((prism_package_import_symbol_t)symbol)
  {
  case PRISM_IMPORT_FONT_6X10_TF: return u8g2_font_6x10_tf;
  case PRISM_IMPORT_FONT_4X6_TF: return u8g2_font_4x6_tf;
  case PRISM_IMPORT_FONT_5X7_MR: return u8g2_font_5x7_mr;
  case PRISM_IMPORT_FONT_5X7_TF: return u8g2_font_5x7_tf;
  case PRISM_IMPORT_FONT_5X7_TR: return u8g2_font_5x7_tr;
  case PRISM_IMPORT_FONT_7X14_MR: return u8g2_font_7x14_mr;
  case PRISM_IMPORT_FONT_7X14B_MR: return u8g2_font_7x14B_mr;
  case PRISM_IMPORT_FONT_U8GLIB_4_TF: return u8g2_font_u8glib_4_tf;
  default: return NULL;
  }
}

static uint32_t guest_malloc(host_cartridge_vm_t *vm, uint32_t size)
{
  if (size == 0)
    size = 1;
  size = align_up(size, 8);
  for (size_t i = 0; i < GUEST_ALLOCATION_MAX; ++i)
    if (!vm->allocations[i].used && vm->allocations[i].address != 0 &&
        vm->allocations[i].size >= size)
    {
      vm->allocations[i].used = true;
      return vm->allocations[i].address;
    }

  size_t free_slot = GUEST_ALLOCATION_MAX;
  for (size_t i = 0; i < GUEST_ALLOCATION_MAX; ++i)
    if (vm->allocations[i].address == 0)
    {
      free_slot = i;
      break;
    }
  if (free_slot == GUEST_ALLOCATION_MAX || size > vm->heap_limit - vm->heap_next)
    return 0;
  uint32_t result = vm->heap_next;
  vm->heap_next += size;
  vm->allocations[free_slot] =
      (guest_allocation_t){.address = result, .size = size, .used = true};
  return result;
}

static guest_allocation_t *guest_allocation(host_cartridge_vm_t *vm,
                                             uint32_t address)
{
  for (size_t i = 0; i < GUEST_ALLOCATION_MAX; ++i)
    if (vm->allocations[i].used && vm->allocations[i].address == address)
      return &vm->allocations[i];
  return NULL;
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
static uint32_t guest_variadic_word(host_cartridge_vm_t *vm,
                                    int first_register,
                                    uint32_t va_list_address,
                                    uint32_t *argument_index)
{
  if (first_register < 0)
  {
    uint32_t result = guest_read_u32(
        vm, va_list_address + *argument_index * 4u);
    ++*argument_index;
    return result;
  }
  uint32_t register_index = (uint32_t)first_register + *argument_index;
  ++*argument_index;
  if (register_index <= UC_ARM_REG_R3)
    return reg_read(vm, (int)register_index);
  return stack_arg(vm, register_index - UC_ARM_REG_R3 - 1u);
}

static uint64_t guest_variadic_u64(host_cartridge_vm_t *vm,
                                   int first_register,
                                   uint32_t va_list_address,
                                   uint32_t *argument_index)
{
  if (first_register < 0)
  {
    uint32_t address = va_list_address + *argument_index * 4u;
    *argument_index += ((0u - address) & 7u) / 4u;
  }
  else
  {
    uint32_t logical_register =
        (uint32_t)(first_register - UC_ARM_REG_R0) + *argument_index;
    if ((logical_register & 1u) != 0)
      ++*argument_index;
  }
  uint32_t low = guest_variadic_word(vm, first_register, va_list_address,
                                     argument_index);
  uint32_t high = guest_variadic_word(vm, first_register, va_list_address,
                                      argument_index);
  return words_u64(low, high);
}

static int guest_format(host_cartridge_vm_t *vm, uint32_t destination,
                        uint32_t capacity, uint32_t format_address,
                        int first_register, uint32_t va_list_address,
                        bool console)
{
  char format[1024];
  if (!guest_string(vm, format_address, format, sizeof(format)))
    return -1;
  char output[8192];
  size_t output_length = 0;
  uint32_t argument_index = 0;
  for (const char *cursor = format; *cursor != '\0';)
  {
    if (*cursor != '%')
    {
      if (output_length + 1 < sizeof(output))
        output[output_length] = *cursor;
      ++output_length;
      ++cursor;
      continue;
    }
    const char *begin = cursor++;
    if (*cursor == '%')
    {
      if (output_length + 1 < sizeof(output))
        output[output_length] = '%';
      ++output_length;
      ++cursor;
      continue;
    }
    while (strchr("-+ #0", *cursor) != NULL)
      ++cursor;
    while (*cursor >= '0' && *cursor <= '9')
      ++cursor;
    if (*cursor == '.')
    {
      ++cursor;
      while (*cursor >= '0' && *cursor <= '9')
        ++cursor;
    }
    char length = 0;
    bool long_long = false;
    if (strchr("hljztL", *cursor) != NULL)
    {
      length = *cursor++;
      if ((*cursor == 'h' && length == 'h') ||
          (*cursor == 'l' && length == 'l'))
      {
        long_long = length == 'l';
        ++cursor;
      }
    }
    char conversion = *cursor == '\0' ? '\0' : *cursor++;
    size_t spec_length = (size_t)(cursor - begin);
    if (spec_length >= 64 || conversion == '\0')
      return -1;
    char spec[64];
    memcpy(spec, begin, spec_length);
    spec[spec_length] = '\0';

    char piece[1024];
    int written = -1;
    if (conversion == 's')
    {
      uint32_t argument = guest_variadic_word(
          vm, first_register, va_list_address, &argument_index);
      char string[768];
      guest_string(vm, argument, string, sizeof(string));
      written = snprintf(piece, sizeof(piece), spec, string);
    }
    else if (conversion == 'c')
    {
      uint32_t argument = guest_variadic_word(
          vm, first_register, va_list_address, &argument_index);
      written = snprintf(piece, sizeof(piece), spec, (int)argument);
    }
    else if (conversion == 'd' || conversion == 'i')
    {
      if (long_long || length == 'j')
      {
        int64_t argument = (int64_t)guest_variadic_u64(
            vm, first_register, va_list_address, &argument_index);
        written = snprintf(piece, sizeof(piece), spec,
                           (long long)argument);
      }
      else
      {
        uint32_t argument = guest_variadic_word(
            vm, first_register, va_list_address, &argument_index);
        if (length == 'l')
        written = snprintf(piece, sizeof(piece), spec, (long)(int32_t)argument);
        else if (length == 'z' || length == 't')
          written = snprintf(piece, sizeof(piece), spec,
                             (ptrdiff_t)(int32_t)argument);
        else
          written = snprintf(piece, sizeof(piece), spec, (int32_t)argument);
      }
    }
    else if (strchr("uoxX", conversion) != NULL)
    {
      if (long_long || length == 'j')
      {
        uint64_t argument = guest_variadic_u64(
            vm, first_register, va_list_address, &argument_index);
        written = snprintf(piece, sizeof(piece), spec,
                           (unsigned long long)argument);
      }
      else
      {
        uint32_t argument = guest_variadic_word(
            vm, first_register, va_list_address, &argument_index);
        if (length == 'l')
          written = snprintf(piece, sizeof(piece), spec,
                             (unsigned long)argument);
        else if (length == 'z' || length == 't')
          written = snprintf(piece, sizeof(piece), spec, (size_t)argument);
        else
          written = snprintf(piece, sizeof(piece), spec, argument);
      }
    }
    else if (conversion == 'p')
    {
      uint32_t argument = guest_variadic_word(
          vm, first_register, va_list_address, &argument_index);
      written = snprintf(piece, sizeof(piece), spec,
                         (void *)(uintptr_t)argument);
    }
    else if (strchr("fFeEgGaA", conversion) != NULL)
    {
      uint64_t words = guest_variadic_u64(
          vm, first_register, va_list_address, &argument_index);
      double argument;
      memcpy(&argument, &words, sizeof(argument));
      written = snprintf(piece, sizeof(piece), spec, argument);
    }
    if (written < 0)
      return -1;
    size_t piece_length = (size_t)written;
    size_t copy = piece_length;
    if (copy > sizeof(piece) - 1)
      copy = sizeof(piece) - 1;
    if (output_length < sizeof(output) - 1)
    {
      size_t available = sizeof(output) - 1 - output_length;
      if (copy > available)
        copy = available;
      memcpy(output + output_length, piece, copy);
    }
    output_length += piece_length;
  }

  size_t stored = output_length;
  if (stored >= sizeof(output))
    stored = sizeof(output) - 1;
  output[stored] = '\0';
  if (console)
  {
    fwrite(output, 1, stored, stdout);
    fflush(stdout);
  }
  if (capacity > 0)
  {
    size_t copy = stored;
    if (copy >= capacity)
      copy = capacity - 1;
    guest_write(vm, destination, output, copy);
    char zero = '\0';
    guest_write(vm, destination + (uint32_t)copy, &zero, 1);
  }
  return output_length > INT_MAX ? INT_MAX : (int)output_length;
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static void update_animations(host_cartridge_vm_t *vm)
{
  uint64_t now = platform_now_us();
  for (size_t i = 0; i < GUEST_ANIMATION_MAX; ++i)
  {
    guest_animation_t *animation = &vm->animations[i];
    if (!animation->active)
      continue;
    uint64_t duration = (uint64_t)animation->duration_ms * 1000u;
    float progress = duration == 0 ? 1.f :
        (float)(now - animation->started_at) / (float)duration;
    if (progress >= 1.f)
      progress = 1.f;
    if (progress < 0.f)
      progress = 0.f;
    float eased = progress;
    if (animation->easing == PRISM_ANIM_EASE_INOUT_QUAD)
      eased = progress < .5f ? 2.f * progress * progress
                            : 1.f - powf(-2.f * progress + 2.f, 2.f) / 2.f;
    else if (animation->easing == PRISM_ANIM_EASE_OUT_CUBIC)
      eased = 1.f - powf(1.f - progress, 3.f);
    int32_t value = animation->start +
                    (int32_t)((animation->end - animation->start) * eased);
    guest_write(vm, animation->subject, &value, sizeof(value));
    if (progress >= 1.f)
      animation->active = false;
  }
}

static void api_dispatch(host_cartridge_vm_t *vm, uint16_t api)
{
  prism_t *context = vm->context;
  if (context == NULL || context->api == NULL)
  {
    vm->failed = true;
    return;
  }
  uint32_t r0 = reg_read(vm, UC_ARM_REG_R0);
  uint32_t r1 = reg_read(vm, UC_ARM_REG_R1);
  uint32_t r2 = reg_read(vm, UC_ARM_REG_R2);
  uint32_t r3 = reg_read(vm, UC_ARM_REG_R3);
  switch (api)
  {
  case 0: reg_write(vm, UC_ARM_REG_R0, GUEST_DISPLAY_HANDLE); break;
  case 1: reg_write(vm, UC_ARM_REG_R0, context->api->ticks()); break;
  case 2:
  {
    uint64_t now = context->api->now_us();
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)now);
    reg_write(vm, UC_ARM_REG_R1, (uint32_t)(now >> 32));
    break;
  }
  case 3:
  {
    uint64_t from = (uint64_t)r0 | ((uint64_t)r1 << 32);
    uint64_t to = (uint64_t)r2 | ((uint64_t)r3 << 32);
    uint64_t result = (uint64_t)context->api->time_diff_us(from, to);
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)result);
    reg_write(vm, UC_ARM_REG_R1, (uint32_t)(result >> 32));
    break;
  }
  case 4:
    reg_write(vm, UC_ARM_REG_R0,
              context->api->button_pressed((prism_button_t)r0));
    break;
  case 5:
    reg_write(vm, UC_ARM_REG_R0,
              context->api->button_edge((prism_button_t)r0));
    break;
  case 6:
    reg_write(vm, UC_ARM_REG_R0,
              float_word(context->api->button_hold_ratio((prism_button_t)r0)));
    break;
  case 7: context->api->buttons_reset(); break;
  case 8: context->api->led_set((uint8_t)r0, (prism_color_t){.hex = r1}); break;
  case 9: reg_write(vm, UC_ARM_REG_R0, GUEST_SYNTH_HANDLE); break;
  case 10:
  {
    prism_power_state_t state = context->api->power_state();
    uint32_t packed = 0;
    memcpy(&packed, &state, sizeof(state));
    reg_write(vm, UC_ARM_REG_R0, packed);
    break;
  }
  case 11: context->api->sleep(); break;
  case 12: context->api->system_reset(); break;
  case 13: context->api->persist(); break;
  case 14: context->api->keep_awake(); break;
  case 15:
  {
    uint32_t callback = stack_arg(vm, 0);
    guest_animation_t *slot = NULL;
    for (size_t i = 0; i < GUEST_ANIMATION_MAX; ++i)
      if (!vm->animations[i].active || vm->animations[i].subject == r0)
      {
        slot = &vm->animations[i];
        break;
      }
    if (slot == NULL || callback != 0)
    {
      reg_write(vm, UC_ARM_REG_R0, (uint32_t)-1);
      break;
    }
    int32_t start = 0;
    guest_read(vm, r0, &start, sizeof(start));
    *slot = (guest_animation_t){
        .subject = r0,
        .start = start,
        .end = (int32_t)r1,
        .started_at = platform_now_us(),
        .duration_ms = r2,
        .easing = (uint8_t)r3,
        .active = true,
    };
    reg_write(vm, UC_ARM_REG_R0, 0);
    break;
  }
  case 16:
    for (size_t i = 0; i < GUEST_ANIMATION_MAX; ++i)
      if (vm->animations[i].active && vm->animations[i].subject == r0)
      {
        if (r1)
          guest_write(vm, r0, &vm->animations[i].end,
                      sizeof(vm->animations[i].end));
        vm->animations[i].active = false;
      }
    break;
  case 17:
  {
    uint8_t data[LEADERBOARD_MAX_DATA_BYTES];
    uint8_t qr[LEADERBOARD_QR_SIZE];
    bool ok = r2 <= sizeof(data) && guest_read(vm, r1, data, r2) &&
              context->api->leaderboard_qrcode((uint8_t)r0, data, r2, qr) &&
              guest_write(vm, r3, qr, sizeof(qr));
    reg_write(vm, UC_ARM_REG_R0, ok);
    break;
  }
  case 18:
    reg_write(vm, UC_ARM_REG_R0,
              context->api->button_keydown((prism_button_t)r0));
    break;
  case 19:
    reg_write(vm, UC_ARM_REG_R0,
              context->api->button_keyup((prism_button_t)r0));
    break;
  case 20:
    reg_write(vm, UC_ARM_REG_R0,
              context->api->button_keydown_tick((prism_button_t)r0));
    break;
  case 21:
    reg_write(vm, UC_ARM_REG_R0,
              context->api->button_keyup_tick((prism_button_t)r0));
    break;
  case 22:
    context->api->ui_sound((prism_ui_sound_t)r0, (uint8_t)r1);
    break;
  default:
    fprintf(stderr, "unsupported cartridge API trap: %u\n", api);
    vm->failed = true;
    break;
  }
}

static void import_dispatch(host_cartridge_vm_t *vm, uint16_t symbol)
{
  uint32_t r0 = reg_read(vm, UC_ARM_REG_R0);
  uint32_t r1 = reg_read(vm, UC_ARM_REG_R1);
  uint32_t r2 = reg_read(vm, UC_ARM_REG_R2);
  uint32_t r3 = reg_read(vm, UC_ARM_REG_R3);
  u8g2_t *display = platform_display_get_u8g2();
  char a[4096];
  char b[4096];
  switch ((prism_package_import_symbol_t)symbol)
  {
  case PRISM_IMPORT_U8G2_SET_DRAW_COLOR:
    u8g2_SetDrawColor(display, (uint8_t)r1);
    break;
  case PRISM_IMPORT_U8G2_SET_FONT:
    u8g2_SetFont(display, font_for_token(r1));
    break;
  case PRISM_IMPORT_U8G2_DRAW_STR:
    guest_string(vm, r3, a, sizeof(a));
    reg_write(vm, UC_ARM_REG_R0,
              u8g2_DrawStr(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2, a));
    break;
  case PRISM_IMPORT_SNPRINTF:
    reg_write(vm, UC_ARM_REG_R0,
              (uint32_t)guest_format(vm, r0, r1, r2, UC_ARM_REG_R3, 0,
                                     false));
    break;
  case PRISM_IMPORT_PRINTF:
    reg_write(vm, UC_ARM_REG_R0,
              (uint32_t)guest_format(vm, 0, 0, r0, UC_ARM_REG_R1, 0, true));
    break;
  case PRISM_IMPORT_VPRINTF:
    reg_write(vm, UC_ARM_REG_R0,
              (uint32_t)guest_format(vm, 0, 0, r0, -1, r1, true));
    break;
  case PRISM_IMPORT_SPRINTF:
    reg_write(vm, UC_ARM_REG_R0,
              (uint32_t)guest_format(vm, r0, 8192, r1, UC_ARM_REG_R2, 0,
                                     false));
    break;
  case PRISM_IMPORT_VSNPRINTF:
    reg_write(vm, UC_ARM_REG_R0,
              (uint32_t)guest_format(vm, r0, r1, r2, -1, r3, false));
    break;
  case PRISM_IMPORT_PUTS:
    if (!guest_string(vm, r0, a, sizeof(a)))
      reg_write(vm, UC_ARM_REG_R0, (uint32_t)EOF);
    else
      reg_write(vm, UC_ARM_REG_R0, (uint32_t)puts(a));
    break;
  case PRISM_IMPORT_PUTCHAR:
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)putchar((unsigned char)r0));
    fflush(stdout);
    break;
  case PRISM_IMPORT_GETCHAR:
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)getchar());
    break;
  case PRISM_IMPORT_U8G2_GET_STR_WIDTH:
    guest_string(vm, r1, a, sizeof(a));
    reg_write(vm, UC_ARM_REG_R0, u8g2_GetStrWidth(display, a));
    break;
  case PRISM_IMPORT_U8G2_DRAW_XBM:
  {
    uint32_t height = stack_arg(vm, 0);
    uint32_t bitmap_address = stack_arg(vm, 1);
    size_t bytes = ((r3 + 7u) / 8u) * height;
    uint8_t *bitmap = malloc(bytes);
    if (bitmap == NULL || !guest_read(vm, bitmap_address, bitmap, bytes))
      vm->failed = true;
    else
      u8g2_DrawXBM(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                   (u8g2_uint_t)r3, (u8g2_uint_t)height, bitmap);
    free(bitmap);
    break;
  }
  case PRISM_IMPORT_U8G2_DRAW_BOX:
    u8g2_DrawBox(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                 (u8g2_uint_t)r3, (u8g2_uint_t)stack_arg(vm, 0));
    break;
  case PRISM_IMPORT_U8G2_DRAW_FRAME:
    u8g2_DrawFrame(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                   (u8g2_uint_t)r3, (u8g2_uint_t)stack_arg(vm, 0));
    break;
  case PRISM_IMPORT_U8G2_DRAW_RBOX:
    u8g2_DrawRBox(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                  (u8g2_uint_t)r3, (u8g2_uint_t)stack_arg(vm, 0),
                  (u8g2_uint_t)stack_arg(vm, 1));
    break;
  case PRISM_IMPORT_U8G2_DRAW_RFRAME:
    u8g2_DrawRFrame(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                    (u8g2_uint_t)r3, (u8g2_uint_t)stack_arg(vm, 0),
                    (u8g2_uint_t)stack_arg(vm, 1));
    break;
  case PRISM_IMPORT_U8G2_DRAW_HLINE:
    u8g2_DrawHLine(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                   (u8g2_uint_t)r3);
    break;
  case PRISM_IMPORT_U8G2_DRAW_VLINE:
    u8g2_DrawVLine(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                   (u8g2_uint_t)r3);
    break;
  case PRISM_IMPORT_U8G2_DRAW_PIXEL:
    u8g2_DrawPixel(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2);
    break;
  case PRISM_IMPORT_U8G2_DRAW_LINE:
    u8g2_DrawLine(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                  (u8g2_uint_t)r3, (u8g2_uint_t)stack_arg(vm, 0));
    break;
  case PRISM_IMPORT_U8G2_DRAW_CIRCLE:
    u8g2_DrawCircle(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                    (u8g2_uint_t)r3, (uint8_t)stack_arg(vm, 0));
    break;
  case PRISM_IMPORT_U8G2_DRAW_DISC:
    u8g2_DrawDisc(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                  (u8g2_uint_t)r3, (uint8_t)stack_arg(vm, 0));
    break;
  case PRISM_IMPORT_U8G2_DRAW_ELLIPSE:
    u8g2_DrawEllipse(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                     (u8g2_uint_t)r3, (u8g2_uint_t)stack_arg(vm, 0),
                     (uint8_t)stack_arg(vm, 1));
    break;
  case PRISM_IMPORT_U8G2_DRAW_FILLED_ELLIPSE:
    u8g2_DrawFilledEllipse(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                           (u8g2_uint_t)r3,
                           (u8g2_uint_t)stack_arg(vm, 0),
                           (uint8_t)stack_arg(vm, 1));
    break;
  case PRISM_IMPORT_U8G2_DRAW_TRIANGLE:
    u8g2_DrawTriangle(display, (int16_t)r1, (int16_t)r2, (int16_t)r3,
                      (int16_t)stack_arg(vm, 0),
                      (int16_t)stack_arg(vm, 1),
                      (int16_t)stack_arg(vm, 2));
    break;
  case PRISM_IMPORT_U8G2_DRAW_ARC:
    u8g2_DrawArc(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2,
                 (u8g2_uint_t)r3, (uint8_t)stack_arg(vm, 0),
                 (uint8_t)stack_arg(vm, 1));
    break;
  case PRISM_IMPORT_U8G2_DRAW_UTF8:
    guest_string(vm, r3, a, sizeof(a));
    reg_write(vm, UC_ARM_REG_R0,
              u8g2_DrawUTF8(display, (u8g2_uint_t)r1, (u8g2_uint_t)r2, a));
    break;
  case PRISM_IMPORT_U8G2_SET_BITMAP_MODE:
    u8g2_SetBitmapMode(display, (uint8_t)r1);
    break;
  case PRISM_IMPORT_MEMCPY:
  case PRISM_IMPORT_MEMMOVE:
  {
    uint8_t *temporary = malloc(r2 == 0 ? 1 : r2);
    if (temporary == NULL || !guest_read(vm, r1, temporary, r2) ||
        !guest_write(vm, r0, temporary, r2))
      vm->failed = true;
    free(temporary);
    reg_write(vm, UC_ARM_REG_R0, r0);
    break;
  }
  case PRISM_IMPORT_MEMSET:
  {
    uint8_t block[256];
    memset(block, (uint8_t)r1, sizeof(block));
    for (uint32_t offset = 0; offset < r2; offset += sizeof(block))
    {
      uint32_t count = r2 - offset;
      if (count > sizeof(block))
        count = sizeof(block);
      guest_write(vm, r0 + offset, block, count);
    }
    reg_write(vm, UC_ARM_REG_R0, r0);
    break;
  }
  case PRISM_IMPORT_AEABI_MEMCPY:
  case PRISM_IMPORT_AEABI_MEMCPY4:
  case PRISM_IMPORT_AEABI_MEMCPY8:
  {
    uint8_t *temporary = malloc(r2 == 0 ? 1 : r2);
    if (temporary == NULL || !guest_read(vm, r1, temporary, r2) ||
        !guest_write(vm, r0, temporary, r2))
      vm->failed = true;
    free(temporary);
    break;
  }
  case PRISM_IMPORT_AEABI_MEMSET:
  case PRISM_IMPORT_AEABI_MEMSET4:
  case PRISM_IMPORT_AEABI_MEMSET8:
  {
    /* ARM EABI orders these arguments as destination, size, value. */
    uint8_t block[256];
    memset(block, (uint8_t)r2, sizeof(block));
    for (uint32_t offset = 0; offset < r1; offset += sizeof(block))
    {
      uint32_t count = r1 - offset;
      if (count > sizeof(block))
        count = sizeof(block);
      if (!guest_write(vm, r0 + offset, block, count))
        vm->failed = true;
    }
    break;
  }
  case PRISM_IMPORT_MEMCMP:
  {
    int result = 0;
    for (uint32_t i = 0; i < r2 && result == 0; ++i)
    {
      uint8_t left = 0, right = 0;
      guest_read(vm, r0 + i, &left, 1);
      guest_read(vm, r1 + i, &right, 1);
      result = (int)left - (int)right;
    }
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)result);
    break;
  }
  case PRISM_IMPORT_STRLEN:
    guest_string(vm, r0, a, sizeof(a));
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)strlen(a));
    break;
  case PRISM_IMPORT_STRCMP:
  case PRISM_IMPORT_STRNCMP:
    guest_string(vm, r0, a, sizeof(a));
    guest_string(vm, r1, b, sizeof(b));
    reg_write(vm, UC_ARM_REG_R0,
              (uint32_t)(symbol == PRISM_IMPORT_STRCMP
                             ? strcmp(a, b)
                             : strncmp(a, b, r2)));
    break;
  case PRISM_IMPORT_STRNCPY:
    guest_string(vm, r1, a, sizeof(a));
    for (uint32_t i = 0; i < r2; ++i)
    {
      char value = i < strlen(a) ? a[i] : '\0';
      guest_write(vm, r0 + i, &value, 1);
    }
    reg_write(vm, UC_ARM_REG_R0, r0);
    break;
  case PRISM_IMPORT_STRCAT:
    guest_string(vm, r0, a, sizeof(a));
    guest_string(vm, r1, b, sizeof(b));
    guest_write(vm, r0 + (uint32_t)strlen(a), b, strlen(b) + 1);
    reg_write(vm, UC_ARM_REG_R0, r0);
    break;
  case PRISM_IMPORT_MALLOC:
    reg_write(vm, UC_ARM_REG_R0, guest_malloc(vm, r0));
    break;
  case PRISM_IMPORT_CALLOC:
  {
    uint32_t size = r1 != 0 && r0 > UINT32_MAX / r1 ? 0 : r0 * r1;
    uint32_t address = size == 0 ? 0 : guest_malloc(vm, size);
    if (address != 0)
    {
      uint8_t zero[256] = {0};
      for (uint32_t i = 0; i < size; i += sizeof(zero))
      {
        uint32_t count = size - i;
        if (count > sizeof(zero))
          count = sizeof(zero);
        guest_write(vm, address + i, zero, count);
      }
    }
    reg_write(vm, UC_ARM_REG_R0, address);
    break;
  }
  case PRISM_IMPORT_REALLOC:
  {
    guest_allocation_t *old = guest_allocation(vm, r0);
    if (r0 == 0)
      reg_write(vm, UC_ARM_REG_R0, guest_malloc(vm, r1));
    else if (r1 == 0)
    {
      if (old != NULL)
        old->used = false;
      reg_write(vm, UC_ARM_REG_R0, 0);
    }
    else
    {
      uint32_t address = guest_malloc(vm, r1);
      if (address != 0 && old != NULL)
      {
        uint32_t count = old->size < r1 ? old->size : r1;
        uint8_t *temporary = malloc(count);
        if (temporary != NULL)
        {
          guest_read(vm, old->address, temporary, count);
          guest_write(vm, address, temporary, count);
          free(temporary);
          old->used = false;
        }
      }
      reg_write(vm, UC_ARM_REG_R0, address);
    }
    break;
  }
  case PRISM_IMPORT_FREE:
  {
    guest_allocation_t *allocation = guest_allocation(vm, r0);
    if (allocation != NULL)
      allocation->used = false;
    break;
  }
  case PRISM_IMPORT_AEABI_IDIV:
  case PRISM_IMPORT_AEABI_IDIVMOD:
  {
    int32_t numerator = (int32_t)r0;
    int32_t denominator = (int32_t)r1;
    int32_t quotient;
    int32_t remainder = 0;
    if (denominator == 0)
      quotient = numerator == 0 ? 0 : numerator < 0 ? INT32_MIN : INT32_MAX;
    else if (numerator == INT32_MIN && denominator == -1)
      quotient = INT32_MIN;
    else
    {
      quotient = numerator / denominator;
      remainder = numerator % denominator;
    }
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)quotient);
    if (symbol == PRISM_IMPORT_AEABI_IDIVMOD)
      reg_write(vm, UC_ARM_REG_R1, (uint32_t)remainder);
    break;
  }
  case PRISM_IMPORT_AEABI_UIDIV:
  case PRISM_IMPORT_AEABI_UIDIVMOD:
  {
    uint32_t quotient = r1 == 0 ? (r0 == 0 ? 0 : UINT32_MAX) : r0 / r1;
    uint32_t remainder = r1 == 0 ? 0 : r0 % r1;
    reg_write(vm, UC_ARM_REG_R0, quotient);
    if (symbol == PRISM_IMPORT_AEABI_UIDIVMOD)
      reg_write(vm, UC_ARM_REG_R1, remainder);
    break;
  }
  case PRISM_IMPORT_AEABI_LDIVMOD:
  {
    int64_t numerator = (int64_t)words_u64(r0, r1);
    int64_t denominator = (int64_t)words_u64(r2, r3);
    int64_t quotient;
    int64_t remainder = 0;
    if (denominator == 0)
      quotient = numerator == 0 ? 0 : numerator < 0 ? INT64_MIN : INT64_MAX;
    else if (numerator == INT64_MIN && denominator == -1)
      quotient = INT64_MIN;
    else
    {
      quotient = numerator / denominator;
      remainder = numerator % denominator;
    }
    reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, (uint64_t)quotient);
    reg_write_u64(vm, UC_ARM_REG_R2, UC_ARM_REG_R3, (uint64_t)remainder);
    break;
  }
  case PRISM_IMPORT_AEABI_ULDIVMOD:
  {
    uint64_t numerator = words_u64(r0, r1);
    uint64_t denominator = words_u64(r2, r3);
    uint64_t quotient = denominator == 0
                            ? (numerator == 0 ? 0 : UINT64_MAX)
                            : numerator / denominator;
    uint64_t remainder = denominator == 0 ? 0 : numerator % denominator;
    reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, quotient);
    reg_write_u64(vm, UC_ARM_REG_R2, UC_ARM_REG_R3, remainder);
    break;
  }
  case PRISM_IMPORT_AEABI_LMUL:
  {
    uint64_t result = words_u64(r0, r1) * words_u64(r2, r3);
    reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, result);
    break;
  }
  case PRISM_IMPORT_CLZSI2:
    reg_write(vm, UC_ARM_REG_R0, leading_zeroes_u64(r0, 32));
    break;
  case PRISM_IMPORT_CLZDI2:
    reg_write(vm, UC_ARM_REG_R0,
              leading_zeroes_u64(words_u64(r0, r1), 64));
    break;
  case PRISM_IMPORT_CTZSI2:
    reg_write(vm, UC_ARM_REG_R0, trailing_zeroes_u64(r0, 32));
    break;
  case PRISM_IMPORT_CTZDI2:
    reg_write(vm, UC_ARM_REG_R0,
              trailing_zeroes_u64(words_u64(r0, r1), 64));
    break;
  case PRISM_IMPORT_POPCOUNTSI2:
    reg_write(vm, UC_ARM_REG_R0, population_count_u64(r0));
    break;
  case PRISM_IMPORT_POPCOUNTDI2:
    reg_write(vm, UC_ARM_REG_R0,
              population_count_u64(words_u64(r0, r1)));
    break;
  case PRISM_IMPORT_SINF: reg_write(vm, UC_ARM_REG_R0, float_word(sinf(word_float(r0)))); break;
  case PRISM_IMPORT_COSF: reg_write(vm, UC_ARM_REG_R0, float_word(cosf(word_float(r0)))); break;
  case PRISM_IMPORT_SQRTF: reg_write(vm, UC_ARM_REG_R0, float_word(sqrtf(word_float(r0)))); break;
  case PRISM_IMPORT_FMODF: reg_write(vm, UC_ARM_REG_R0, float_word(fmodf(word_float(r0), word_float(r1)))); break;
  case PRISM_IMPORT_FLOORF: reg_write(vm, UC_ARM_REG_R0, float_word(floorf(word_float(r0)))); break;
  case PRISM_IMPORT_EXPF: reg_write(vm, UC_ARM_REG_R0, float_word(expf(word_float(r0)))); break;
  case PRISM_IMPORT_FABSF: reg_write(vm, UC_ARM_REG_R0, float_word(fabsf(word_float(r0)))); break;
  case PRISM_IMPORT_ATAN2F: reg_write(vm, UC_ARM_REG_R0, float_word(atan2f(word_float(r0), word_float(r1)))); break;
  case PRISM_IMPORT_FMAXF: reg_write(vm, UC_ARM_REG_R0, float_word(fmaxf(word_float(r0), word_float(r1)))); break;
  case PRISM_IMPORT_AEABI_FADD: reg_write(vm, UC_ARM_REG_R0, float_word(word_float(r0) + word_float(r1))); break;
  case PRISM_IMPORT_AEABI_FDIV: reg_write(vm, UC_ARM_REG_R0, float_word(word_float(r0) / word_float(r1))); break;
  case PRISM_IMPORT_AEABI_FMUL: reg_write(vm, UC_ARM_REG_R0, float_word(word_float(r0) * word_float(r1))); break;
  case PRISM_IMPORT_AEABI_FRSUB: reg_write(vm, UC_ARM_REG_R0, float_word(word_float(r1) - word_float(r0))); break;
  case PRISM_IMPORT_AEABI_FSUB: reg_write(vm, UC_ARM_REG_R0, float_word(word_float(r0) - word_float(r1))); break;
  case PRISM_IMPORT_AEABI_CFCMPEQ:
  case PRISM_IMPORT_AEABI_CFCMPLE:
    fp_compare_flags(vm, word_float(r0), word_float(r1));
    break;
  case PRISM_IMPORT_AEABI_CFRCMPLE:
    fp_compare_flags(vm, word_float(r1), word_float(r0));
    break;
  case PRISM_IMPORT_AEABI_FCMPEQ: reg_write(vm, UC_ARM_REG_R0, word_float(r0) == word_float(r1)); break;
  case PRISM_IMPORT_AEABI_FCMPLT: reg_write(vm, UC_ARM_REG_R0, word_float(r0) < word_float(r1)); break;
  case PRISM_IMPORT_AEABI_FCMPLE: reg_write(vm, UC_ARM_REG_R0, word_float(r0) <= word_float(r1)); break;
  case PRISM_IMPORT_AEABI_FCMPGE: reg_write(vm, UC_ARM_REG_R0, word_float(r0) >= word_float(r1)); break;
  case PRISM_IMPORT_AEABI_FCMPGT: reg_write(vm, UC_ARM_REG_R0, word_float(r0) > word_float(r1)); break;
  case PRISM_IMPORT_AEABI_FCMPUN: reg_write(vm, UC_ARM_REG_R0, isnan(word_float(r0)) || isnan(word_float(r1))); break;
  case PRISM_IMPORT_AEABI_I2F: reg_write(vm, UC_ARM_REG_R0, float_word((float)(int32_t)r0)); break;
  case PRISM_IMPORT_AEABI_UI2F: reg_write(vm, UC_ARM_REG_R0, float_word((float)r0)); break;
  case PRISM_IMPORT_AEABI_F2IZ: reg_write(vm, UC_ARM_REG_R0, (uint32_t)float_to_i32(word_float(r0))); break;
  case PRISM_IMPORT_AEABI_F2UIZ: reg_write(vm, UC_ARM_REG_R0, float_to_u32(word_float(r0))); break;
  case PRISM_IMPORT_AEABI_L2F: reg_write(vm, UC_ARM_REG_R0, float_word((float)(int64_t)words_u64(r0, r1))); break;
  case PRISM_IMPORT_AEABI_UL2F: reg_write(vm, UC_ARM_REG_R0, float_word((float)words_u64(r0, r1))); break;
  case PRISM_IMPORT_AEABI_F2LZ: reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, (uint64_t)float_to_i64(word_float(r0))); break;
  case PRISM_IMPORT_AEABI_F2ULZ: reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, float_to_u64(word_float(r0))); break;
  case PRISM_IMPORT_AEABI_F2D: reg_write_double(vm, (double)word_float(r0)); break;
  case PRISM_IMPORT_AEABI_DADD: reg_write_double(vm, words_double(r0, r1) + words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DDIV: reg_write_double(vm, words_double(r0, r1) / words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DMUL: reg_write_double(vm, words_double(r0, r1) * words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DRSUB: reg_write_double(vm, words_double(r2, r3) - words_double(r0, r1)); break;
  case PRISM_IMPORT_AEABI_DSUB: reg_write_double(vm, words_double(r0, r1) - words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_CDCMPEQ:
  case PRISM_IMPORT_AEABI_CDCMPLE:
    fp_compare_flags(vm, words_double(r0, r1), words_double(r2, r3));
    break;
  case PRISM_IMPORT_AEABI_CDRCMPLE:
    fp_compare_flags(vm, words_double(r2, r3), words_double(r0, r1));
    break;
  case PRISM_IMPORT_AEABI_DCMPEQ: reg_write(vm, UC_ARM_REG_R0, words_double(r0, r1) == words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DCMPLT: reg_write(vm, UC_ARM_REG_R0, words_double(r0, r1) < words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DCMPLE: reg_write(vm, UC_ARM_REG_R0, words_double(r0, r1) <= words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DCMPGE: reg_write(vm, UC_ARM_REG_R0, words_double(r0, r1) >= words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DCMPGT: reg_write(vm, UC_ARM_REG_R0, words_double(r0, r1) > words_double(r2, r3)); break;
  case PRISM_IMPORT_AEABI_DCMPUN: reg_write(vm, UC_ARM_REG_R0, isnan(words_double(r0, r1)) || isnan(words_double(r2, r3))); break;
  case PRISM_IMPORT_AEABI_I2D: reg_write_double(vm, (double)(int32_t)r0); break;
  case PRISM_IMPORT_AEABI_UI2D: reg_write_double(vm, (double)r0); break;
  case PRISM_IMPORT_AEABI_L2D: reg_write_double(vm, (double)(int64_t)words_u64(r0, r1)); break;
  case PRISM_IMPORT_AEABI_UL2D: reg_write_double(vm, (double)words_u64(r0, r1)); break;
  case PRISM_IMPORT_AEABI_D2IZ: reg_write(vm, UC_ARM_REG_R0, (uint32_t)float_to_i32(words_double(r0, r1))); break;
  case PRISM_IMPORT_AEABI_D2UIZ: reg_write(vm, UC_ARM_REG_R0, float_to_u32(words_double(r0, r1))); break;
  case PRISM_IMPORT_AEABI_D2LZ: reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, (uint64_t)float_to_i64(words_double(r0, r1))); break;
  case PRISM_IMPORT_AEABI_D2ULZ: reg_write_u64(vm, UC_ARM_REG_R0, UC_ARM_REG_R1, float_to_u64(words_double(r0, r1))); break;
  case PRISM_IMPORT_AEABI_D2F: reg_write(vm, UC_ARM_REG_R0, float_word((float)words_double(r0, r1))); break;
  case PRISM_IMPORT_ABS: reg_write(vm, UC_ARM_REG_R0, (uint32_t)abs((int32_t)r0)); break;
  case PRISM_IMPORT_RAND:
    reg_write(vm, UC_ARM_REG_R0, host_cartridge_rand31());
    break;
  case PRISM_IMPORT_SRAND: srand(r0); break;
  case PRISM_IMPORT_QRCODE_GET_SIZE:
  {
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    guest_read(vm, r0, qr, sizeof(qr));
    reg_write(vm, UC_ARM_REG_R0, (uint32_t)qrcodegen_getSize(qr));
    break;
  }
  case PRISM_IMPORT_QRCODE_GET_MODULE:
  {
    uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    guest_read(vm, r0, qr, sizeof(qr));
    reg_write(vm, UC_ARM_REG_R0, qrcodegen_getModule(qr, (int)r1, (int)r2));
    break;
  }
  case PRISM_IMPORT_AUDIO_SYNTH_ENQUEUE:
  {
    uint8_t guest_message[HOST_GUEST_SYNTH_MESSAGE_BYTES];
    audio_synth_message_t message;
    bool ok = guest_read(vm, r1, guest_message, sizeof(guest_message)) &&
              host_cartridge_decode_synth_message(guest_message, &message) &&
              audio_synth_enqueue(vm->context->api->synth(), &message);
    reg_write(vm, UC_ARM_REG_R0, ok);
    break;
  }
  case PRISM_IMPORT_AUDIO_SYNTH_PATCH_CONFIG_SET:
  {
    uint8_t guest_patch[HOST_GUEST_SYNTH_PATCH_BYTES];
    audio_synth_patch_config_t config;
    guest_patch[0] = (uint8_t)r2;
    guest_patch[1] = (uint8_t)(r2 >> 8);
    guest_patch[2] = (uint8_t)(r2 >> 16);
    guest_patch[3] = (uint8_t)(r2 >> 24);
    guest_patch[4] = (uint8_t)r3;
    guest_patch[5] = (uint8_t)(r3 >> 8);
    guest_patch[6] = (uint8_t)(r3 >> 16);
    guest_patch[7] = (uint8_t)(r3 >> 24);
    if (!guest_read(vm, reg_read(vm, UC_ARM_REG_SP), guest_patch + 8,
                    sizeof(guest_patch) - 8) ||
        !host_cartridge_decode_synth_patch(guest_patch, &config))
      vm->failed = true;
    else
      audio_synth_patch_config_set(vm->context->api->synth(), (uint8_t)r1,
                                   config);
    break;
  }
  case PRISM_IMPORT_AUDIO_SYNTH_PANIC_SYNC:
    audio_synth_panic_sync(vm->context->api->synth());
    break;
  default:
    if (symbol < PRISM_PACKAGE_MAX_IMPORTS &&
        !vm->unsupported_reported[symbol])
    {
      vm->unsupported_reported[symbol] = true;
      fprintf(stderr, "unsupported cartridge import 0x%04x\n", symbol);
    }
    vm->failed = true;
    break;
  }
}

static void code_hook(uc_engine *uc, uint64_t address, uint32_t size,
                      void *user)
{
  (void)uc;
  (void)size;
  host_cartridge_vm_t *vm = user;
  if (address == GUEST_STOP_ADDRESS)
  {
    uc_emu_stop(vm->uc);
    return;
  }
  if (address < GUEST_TRAP_BASE || address >= GUEST_TRAP_BASE + GUEST_TRAP_BYTES)
    return;
  uint32_t slot = (uint32_t)(address - GUEST_TRAP_BASE) / 4u;
  if (slot >= GUEST_API_TRAP_BASE)
    api_dispatch(vm, (uint16_t)(slot - GUEST_API_TRAP_BASE));
  else
    import_dispatch(vm, (uint16_t)slot);
  uint32_t link = reg_read(vm, UC_ARM_REG_LR);
  reg_write(vm, UC_ARM_REG_PC, link);
  if (vm->failed)
    uc_emu_stop(vm->uc);
}

static bool invalid_memory_hook(uc_engine *uc, uc_mem_type type,
                                uint64_t address, int size, int64_t value,
                                void *user)
{
  (void)uc;
  (void)value;
  host_cartridge_vm_t *vm = user;
  fprintf(stderr,
          "cartridge invalid memory access: type=%d address=0x%08" PRIx64
          " size=%d\n",
          type, address, size);
  vm->failed = true;
  return false;
}

static uint32_t trap_address(uint16_t slot)
{
  return GUEST_TRAP_BASE + (uint32_t)slot * 4u + 1u;
}

static uint32_t resolve_launch_import(uint16_t symbol, void *user)
{
  (void)user;
  return import_is_object(symbol)
             ? GUEST_OBJECT_BASE + (uint32_t)symbol * 4u
             : trap_address(symbol);
}

host_cartridge_vm_t *
host_cartridge_vm_create(const host_cartridge_package_t *package)
{
  if (package == NULL || package->bytes == NULL)
    return NULL;
  host_cartridge_vm_t *vm = calloc(1, sizeof(*vm));
  uint8_t *launch_image = NULL;
  if (vm == NULL)
    return NULL;
  vm->package = package;
  uc_err error = uc_open(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS, &vm->uc);
  if (error != UC_ERR_OK ||
      uc_ctl_set_cpu_model(vm->uc, UC_CPU_ARM_CORTEX_M0) != UC_ERR_OK)
    goto fail;

  uint32_t image_bytes =
      align_up(package->header.image_size + package->header.rw_size, 0x1000u);
  if (uc_mem_map(vm->uc, GUEST_IMAGE_BASE, image_bytes, UC_PROT_ALL) !=
          UC_ERR_OK ||
      uc_mem_map(vm->uc, GUEST_RAM_BASE, GUEST_RAM_BYTES, UC_PROT_ALL) !=
          UC_ERR_OK ||
      uc_mem_map(vm->uc, GUEST_TRAP_BASE, GUEST_TRAP_BYTES,
                 UC_PROT_READ | UC_PROT_EXEC) != UC_ERR_OK ||
      uc_mem_map(vm->uc, GUEST_STOP_ADDRESS, 0x1000u,
                 UC_PROT_READ | UC_PROT_EXEC) != UC_ERR_OK)
    goto fail;

  if (!guest_write(vm, GUEST_IMAGE_BASE,
                   package->bytes + package->header.image_offset,
                   package->header.image_size))
    goto fail;

  size_t launch_image_size =
      package->header.got_size + package->header.rw_size;
  if (launch_image_size != 0)
  {
    launch_image = malloc(launch_image_size);
    if (launch_image == NULL)
      goto fail;
  }
  uint8_t *got = launch_image;
  uint8_t *rw = launch_image == NULL
                    ? NULL
                    : launch_image + package->header.got_size;
  if (!prism_package_prepare_launch_image(
          package->bytes, &package->header, GUEST_IMAGE_BASE, got,
          GUEST_IMAGE_BASE + package->header.image_size, rw,
          resolve_launch_import, vm) ||
      !guest_write(vm,
                   GUEST_IMAGE_BASE + package->header.got_offset -
                       package->header.image_offset,
                   got, package->header.got_size) ||
      !guest_write(vm, GUEST_IMAGE_BASE + package->header.image_size, rw,
                   package->header.rw_size))
    goto fail;
  free(launch_image);
  launch_image = NULL;

  const prism_package_relocation_t *relocations =
      (const void *)(package->bytes + package->header.relocations_offset);
  uint32_t image_end = package->header.image_offset + package->header.image_size;
  uint32_t got_end = package->header.got_offset + package->header.got_size;
  for (uint32_t i = 0; i < package->header.relocation_count; ++i)
  {
    uint32_t patch = relocations[i].patch_offset;
    if ((patch >= package->header.got_offset && patch < got_end) ||
        patch >= package->header.rw_offset)
      continue;
    uint32_t guest_patch;
    if (patch >= package->header.image_offset && patch < image_end)
      guest_patch = GUEST_IMAGE_BASE + patch - package->header.image_offset;
    else
      guest_patch = GUEST_IMAGE_BASE + package->header.image_size +
                    patch - package->header.rw_offset;
    uint32_t value = guest_read_u32(vm, guest_patch);
    guest_write_u32(vm, guest_patch, value + GUEST_IMAGE_BASE);
  }

  uint32_t api[2 + PRISM_API_V1_FUNCTION_COUNT] = {
      PRISM_API_ABI_VERSION,
      PRISM_API_V1_STRUCT_SIZE,
  };
  for (uint16_t i = 0; i < PRISM_API_V1_FUNCTION_COUNT; ++i)
    api[2 + i] = trap_address(GUEST_API_TRAP_BASE + i);
  if (!guest_write(vm, GUEST_API_ADDRESS, api, sizeof(api)))
    goto fail;

  uint8_t return_instruction[2] = {0x70, 0x47}; /* bx lr */
  for (uint32_t offset = 0; offset < GUEST_TRAP_BYTES; offset += 4u)
    if (uc_mem_write(vm->uc, GUEST_TRAP_BASE + offset, return_instruction,
                     sizeof(return_instruction)) != UC_ERR_OK)
      goto fail;
  if (uc_mem_write(vm->uc, GUEST_STOP_ADDRESS, return_instruction,
                   sizeof(return_instruction)) != UC_ERR_OK)
    goto fail;

  uc_hook code;
  uc_hook memory;
  if (uc_hook_add(vm->uc, &code, UC_HOOK_CODE, code_hook, vm,
                  GUEST_TRAP_BASE,
                  GUEST_TRAP_BASE + GUEST_TRAP_BYTES - 1u) != UC_ERR_OK ||
      uc_hook_add(vm->uc, &code, UC_HOOK_CODE, code_hook, vm,
                  GUEST_STOP_ADDRESS, GUEST_STOP_ADDRESS + 1u) != UC_ERR_OK ||
      uc_hook_add(vm->uc, &memory, UC_HOOK_MEM_INVALID, invalid_memory_hook, vm,
                  1, 0) != UC_ERR_OK)
    goto fail;

  vm->persistent_address = GUEST_PERSISTENT_ADDRESS;
  vm->heap_next = align_up(vm->persistent_address +
                               (uint32_t)package->descriptor.persistent_size,
                           8);
  vm->heap_limit = GUEST_STACK_ADDRESS;
  if (vm->heap_next > vm->heap_limit)
    goto fail;
  return vm;

fail:
  free(launch_image);
  if (vm->uc != NULL)
    uc_close(vm->uc);
  free(vm);
  return NULL;
}

void host_cartridge_vm_destroy(host_cartridge_vm_t *vm)
{
  if (vm == NULL)
    return;
  if (vm->uc != NULL)
    uc_close(vm->uc);
  free(vm);
}

bool host_cartridge_vm_call(host_cartridge_vm_t *vm, uint32_t function,
                            prism_t *context)
{
  if (vm == NULL || context == NULL || function == GUEST_IMAGE_BASE)
    return function == GUEST_IMAGE_BASE;
  /* A Unicorn memory/ABI fault leaves the guest in an undefined state. Do
   * not restart lifecycle entry points on that same VM every engine frame. */
  if (vm->failed)
    return false;
  vm->context = context;
  if (!guest_write(vm, vm->persistent_address, context->persistent,
                   context->persistent_size))
  {
    vm->failure_pending = true;
    return false;
  }
  update_animations(vm);
  if (vm->failed)
  {
    vm->failure_pending = true;
    return false;
  }

  uint32_t guest_context[4] = {
      GUEST_API_ADDRESS,
      vm->package->guest_descriptor,
      context->persistent_size == 0 ? 0 : vm->persistent_address,
      (uint32_t)context->persistent_size,
  };
  if (!guest_write(vm, GUEST_CONTEXT_ADDRESS, guest_context,
                   sizeof(guest_context)))
  {
    vm->failure_pending = true;
    return false;
  }

  uint32_t stack = GUEST_STACK_ADDRESS + GUEST_STACK_BYTES - 8u;
  uint32_t got = vm->package->header.got_size == 0
                     ? 0
                     : GUEST_IMAGE_BASE + vm->package->header.got_offset -
                           vm->package->header.image_offset +
                           vm->package->header.got_base_offset;
  reg_write(vm, UC_ARM_REG_R0, GUEST_CONTEXT_ADDRESS);
  reg_write(vm, UC_ARM_REG_R9, got);
  reg_write(vm, UC_ARM_REG_SP, stack);
  reg_write(vm, UC_ARM_REG_LR, GUEST_STOP_ADDRESS | 1u);

  uc_err error = uc_emu_start(vm->uc, function, 0, 0, 0);
  if (error != UC_ERR_OK || vm->failed)
  {
    uint32_t pc = reg_read(vm, UC_ARM_REG_PC);
    fprintf(stderr, "cartridge stopped at 0x%08x: %s\n", pc,
            uc_strerror(error));
    vm->failed = true;
    vm->failure_pending = true;
    return false;
  }
  if (!guest_read(vm, vm->persistent_address, context->persistent,
                  context->persistent_size))
  {
    vm->failure_pending = true;
    return false;
  }
  return true;
}

bool host_cartridge_vm_take_failure(host_cartridge_vm_t *vm)
{
  if (vm == NULL || !vm->failure_pending)
    return false;
  vm->failure_pending = false;
  return true;
}
