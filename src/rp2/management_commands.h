#pragma once

#include "management_internal.h"

#include <stdint.h>

#include <prism/management_protocol.h>

void management_commands_handle(const prism_management_header_t *request,
                                const uint8_t *payload,
                                management_session_t *session);
