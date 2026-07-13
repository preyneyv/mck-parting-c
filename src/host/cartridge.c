#include <platform/cartridge.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cartridge.h"
#include "cartridge_vm.h"
#include <shared/audio/synth_internal.h>
#include <shared/engine.h>
#include <prism/runtime.h>
#include <state_lifecycle_protocol.h>

enum
{
  HOST_BUNDLED_CARTRIDGE_COUNT = 4,
};

static host_cartridge_package_t bundled[HOST_BUNDLED_CARTRIDGE_COUNT];
static size_t bundled_count;
static host_cartridge_package_t command_line;
static bool command_line_valid;

static const char *const bundled_paths[HOST_BUNDLED_CARTRIDGE_COUNT] = {
    PRISM_HOST_BONGOCAT_PATH,
    PRISM_HOST_MORSE_PATH,
    PRISM_HOST_ASTEROIDS_PATH,
    PRISM_HOST_BEATLINE_PATH,
};

bool host_cartridge_init_bundled(void)
{
  for (size_t i = 0; i < bundled_count; ++i)
    host_cartridge_package_unload(&bundled[i]);
  bundled_count = 0;
  for (size_t i = 0; i < HOST_BUNDLED_CARTRIDGE_COUNT; ++i)
  {
    if (!host_cartridge_package_load(&bundled[i], bundled_paths[i]))
    {
      for (size_t j = 0; j < bundled_count; ++j)
        host_cartridge_package_unload(&bundled[j]);
      bundled_count = 0;
      return false;
    }
    bundled_count++;
  }
  return true;
}

bool host_cartridge_load(const char *path)
{
  host_cartridge_package_unload(&command_line);
  command_line_valid = host_cartridge_package_load(&command_line, path);
  return command_line_valid;
}

size_t platform_cartridge_installed_count(void)
{
  return bundled_count + (command_line_valid ? 1u : 0u);
}

const prism_cartridge_t *platform_cartridge_installed_get(size_t index)
{
  if (index < bundled_count)
    return &bundled[index].descriptor;
  return command_line_valid && index == bundled_count
             ? &command_line.descriptor
             : NULL;
}

static const host_cartridge_package_t *package_for(
    const prism_cartridge_t *cartridge)
{
  for (size_t i = 0; i < bundled_count; ++i)
    if (cartridge == &bundled[i].descriptor)
      return &bundled[i];
  if (command_line_valid && cartridge == &command_line.descriptor)
    return &command_line;
  return NULL;
}

static bool lifecycle_report_valid(
    const prism_state_lifecycle_report_t *report)
{
  return report->magic == PRISM_STATE_LIFECYCLE_REPORT_MAGIC &&
         report->zero_value == 0 &&
         report->initialized_value == 0x12345678u &&
         report->first_number == 11 && report->second_number == 22 &&
         report->pointer_to_number_value == 22 &&
         report->pointer_to_zero_matches == 1 &&
         report->large_bss_first == 0 && report->large_bss_last == 0 &&
         report->writable_string_first == 'r';
}

bool host_cartridge_test_lifecycle(const char *path)
{
  host_cartridge_package_t package;
  if (!host_cartridge_package_load(&package, path))
    return false;
  if (package.descriptor.persistent_size !=
      sizeof(prism_state_lifecycle_report_t))
  {
    host_cartridge_package_unload(&package);
    return false;
  }

  bool passed = true;
  for (uint8_t launch = 0; launch < 2 && passed; ++launch)
  {
    host_cartridge_vm_t *vm = host_cartridge_vm_create(&package);
    prism_state_lifecycle_report_t report = {0};
    prism_t context = {
        .cartridge = &package.descriptor,
        .persistent = &report,
        .persistent_size = sizeof(report),
    };
    passed = vm != NULL && host_cartridge_vm_call(
                               vm,
                               (uint32_t)(uintptr_t)package.descriptor.enter,
                               &context) &&
             lifecycle_report_valid(&report);
    host_cartridge_vm_destroy(vm);
  }
  host_cartridge_package_unload(&package);
  puts(passed ? "host cartridge lifecycle tests passed"
              : "host cartridge lifecycle tests failed");
  return passed;
}

bool host_cartridge_test_audio(const char *path)
{
  enum { TEST_AUDIO_BUFFER_SIZE = 256 };

  host_cartridge_package_t package;
  if (!host_cartridge_package_load(&package, path))
    return false;

  engine_init();
  host_cartridge_vm_t *vm = host_cartridge_vm_create(&package);
  prism_t context = {
      .api = prism_os_api(),
      .cartridge = &package.descriptor,
  };
  bool passed = vm != NULL && host_cartridge_vm_call(
                                  vm,
                                  (uint32_t)(uintptr_t)package.descriptor.enter,
                                  &context);
  uint32_t buffer[TEST_AUDIO_BUFFER_SIZE];
  bool signal = false;
  for (uint8_t attempt = 0; attempt < 4 && passed && !signal; ++attempt)
  {
    audio_synth_fill_buffer(engine_synth(), buffer, TEST_AUDIO_BUFFER_SIZE);
    for (size_t i = 0; i < TEST_AUDIO_BUFFER_SIZE; ++i)
      if (buffer[i] != 0)
      {
        signal = true;
        break;
      }
  }
  passed = passed && signal;
  if (vm != NULL && package.descriptor.leave != NULL)
    host_cartridge_vm_call(vm,
                           (uint32_t)(uintptr_t)package.descriptor.leave,
                           &context);
  host_cartridge_vm_destroy(vm);
  host_cartridge_package_unload(&package);
  puts(passed ? "host cartridge audio tests passed"
              : "host cartridge audio tests failed");
  return passed;
}

bool platform_cartridge_prepare(const prism_cartridge_t *cartridge,
                                platform_cartridge_execution_t *execution)
{
  if (cartridge == NULL || execution == NULL)
    return false;
  memset(execution, 0, sizeof(*execution));
  const host_cartridge_package_t *package = package_for(cartridge);
  if (package == NULL)
    return true;
  execution->backend = host_cartridge_vm_create(package);
  return execution->backend != NULL;
}

void platform_cartridge_release(platform_cartridge_execution_t *execution)
{
  if (execution == NULL)
    return;
  if (execution->backend != NULL)
    host_cartridge_vm_destroy(execution->backend);
  free(execution->allocation);
  memset(execution, 0, sizeof(*execution));
}

void platform_cartridge_call(const platform_cartridge_execution_t *execution,
                             prism_lifecycle_fn function, prism_t *context)
{
  if (function == NULL)
    return;
  if (execution == NULL || execution->backend == NULL)
  {
    function(context);
    return;
  }

  if (!host_cartridge_vm_call(execution->backend,
                              (uint32_t)(uintptr_t)function, context) &&
      host_cartridge_vm_take_failure(execution->backend))
    fprintf(stderr, "cartridge lifecycle call failed at 0x%08lx\n",
            (unsigned long)(uintptr_t)function);
}
