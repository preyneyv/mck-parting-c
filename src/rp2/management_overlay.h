#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <prism/management_protocol.h>

typedef enum
{
  MANAGEMENT_OVERLAY_INSTALL,
  MANAGEMENT_OVERLAY_DELETE,
  MANAGEMENT_OVERLAY_COMPACT,
} management_overlay_operation_t;

void management_overlay_start(management_overlay_operation_t operation,
                              uint32_t total, const char *name,
                              const uint8_t *icon);
void management_overlay_set_completed(uint32_t completed);
void management_overlay_set_progress(uint32_t completed, uint32_t total);
void management_overlay_finish(prism_management_status_t status);
void management_overlay_cancel_install(void);
bool management_overlay_active(void);
void management_overlay_render(void);
