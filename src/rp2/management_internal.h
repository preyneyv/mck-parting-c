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
  bool launch_deferred;
  prism_management_header_t deferred_compact_request;
  prism_management_header_t deferred_launch_request;
  prism_management_cartridge_id_t deferred_launch_id;
  platform_time_t last_heartbeat;
  platform_time_t next_mirror;
} management_session_t;
