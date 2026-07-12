#include "flash_io.h"

#include <stddef.h>
#include <stdint.h>

#include <hardware/address_mapped.h>
#include <hardware/flash.h>
#include <pico/config.h>
#include <pico/flash.h>

#include <platform/system.h>

#define FLASH_SAFE_TIMEOUT_MS 1000u
#define TRACE_ERASE 0x45524153u
#define TRACE_PROGRAM 0x50524f47u

typedef struct
{
  uint32_t offset;
  uint32_t size;
  const uint8_t *data;
} flash_operation_t;

static bool range_valid(uint32_t offset, uint32_t size)
{
  return size != 0 && offset <= PICO_FLASH_SIZE_BYTES &&
         size <= PICO_FLASH_SIZE_BYTES - offset;
}

static bool source_in_xip(const uint8_t *data, uint32_t size)
{
  uintptr_t source = (uintptr_t)data;
  if (size > UINTPTR_MAX - source)
    return true;
  uintptr_t end = source + size;
  return source < XIP_BASE + PICO_FLASH_SIZE_BYTES && end > XIP_BASE;
}

static void erase_callback(void *parameter)
{
  flash_operation_t *operation = parameter;
  flash_range_erase(operation->offset, operation->size);
}

static void program_callback(void *parameter)
{
  flash_operation_t *operation = parameter;
  flash_range_program(operation->offset, operation->data, operation->size);
}

static bool execute(void (*callback)(void *), flash_operation_t *operation,
                    uint32_t trace)
{
  uint32_t parent_stage = platform_watchdog_trace_stage();
  uint32_t parent_detail = platform_watchdog_trace_detail();
  platform_watchdog_trace(trace, operation->offset);
  bool success =
      flash_safe_execute(callback, operation, FLASH_SAFE_TIMEOUT_MS) == PICO_OK;
  platform_watchdog_update();
  platform_watchdog_trace(parent_stage, parent_detail);
  return success;
}

bool prism_flash_erase(uint32_t offset, uint32_t size)
{
  if (!range_valid(offset, size) || offset % FLASH_SECTOR_SIZE != 0 ||
      size % FLASH_SECTOR_SIZE != 0)
    return false;
  flash_operation_t operation = {.offset = offset, .size = size};
  return execute(erase_callback, &operation, TRACE_ERASE);
}

bool prism_flash_program(uint32_t offset, const uint8_t *data, uint32_t size)
{
  if (data == NULL || !range_valid(offset, size) ||
      offset % FLASH_PAGE_SIZE != 0 || size % FLASH_PAGE_SIZE != 0 ||
      source_in_xip(data, size))
    return false;
  flash_operation_t operation = {
      .offset = offset,
      .size = size,
      .data = data,
  };
  return execute(program_callback, &operation, TRACE_PROGRAM);
}
