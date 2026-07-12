#pragma once

#include <stdbool.h>

#include <platform/time.h>
#include <prism/management_protocol.h>

typedef struct
{
  bool active;
  bool mirror_subscribed;
  bool processing_sleep_messages;
  bool sleep_wake_requested;
  bool compact_deferred;
  prism_management_header_t deferred_compact_request;
  platform_time_t last_heartbeat;
  platform_time_t next_mirror;
} management_session_t;
