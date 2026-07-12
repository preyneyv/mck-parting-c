#include "management.h"

#include "cartridge_storage.h"
#include "management_commands.h"
#include "management_internal.h"
#include "management_overlay.h"
#include "management_protocol.h"
#include "management_transport.h"

#include <stdio.h>

#include <hardware/watchdog.h>
#include <tusb.h>

#include <platform/input.h>
#include <platform/system.h>
#include <platform/time.h>
#include <prism/runtime.h>
#include <shared/engine.h>
#include <shared/os/settings.h>

#define HEARTBEAT_TIMEOUT_US 3000000u
#define MIRROR_INTERVAL_US 16667u

static management_session_t session;

static void dispatch(const prism_management_header_t *header,
                     const uint8_t *payload, void *context)
{
  management_commands_handle(header, payload, context);
}

static void disconnect_session(void)
{
  cartridge_storage_install_abort();
  management_overlay_cancel_install();
  session.active = false;
  session.mirror_subscribed = false;
  session.compact_deferred = false;
  platform_input_set_remote_mask(0);
  management_transport_reset();
  prism_settings_set_save_deferred(false);
  prism_cartridge_persistence_set_deferred(false);
}

void management_init(void)
{
  uint32_t previous_stage = platform_watchdog_trace_stage();
  uint32_t previous_detail = platform_watchdog_trace_detail();
  bool watchdog_reset = watchdog_caused_reboot();
  cartridge_storage_init();
  management_transport_init();
  if (watchdog_reset || previous_stage != 0)
  {
    char diagnostic[96];
    snprintf(diagnostic, sizeof(diagnostic),
             "boot diagnostic: watchdog=%u stage=%08lx detail=%08lx\n",
             watchdog_reset ? 1u : 0u, (unsigned long)previous_stage,
             (unsigned long)previous_detail);
    management_transport_set_boot_diagnostic(diagnostic);
    printf("%s", diagnostic);
  }
  platform_watchdog_trace(0, 0);
  disconnect_session();
}

void management_task(void)
{
  if (!tud_mounted())
  {
    if (session.active)
      disconnect_session();
    return;
  }

  if (session.compact_deferred)
  {
    prism_management_header_t request = session.deferred_compact_request;
    session.compact_deferred = false;
    management_commands_handle(&request, NULL, &session);
  }

  management_transport_drain();
  management_transport_receive(dispatch, &session);

  platform_time_t now = platform_now_us();
  if (session.active &&
      platform_time_diff_us(session.last_heartbeat, now) >
          HEARTBEAT_TIMEOUT_US)
  {
    disconnect_session();
    return;
  }

  if (session.active)
    management_transport_queue_logs();
  management_transport_drain();

  if (management_overlay_active())
  {
    engine_mark_input();
    management_overlay_render();
  }
  if (session.mirror_subscribed && platform_time_reached(session.next_mirror))
  {
    if (management_transport_empty())
      management_protocol_mirror();
    session.next_mirror = now + MIRROR_INTERVAL_US;
  }
  management_transport_drain();
}

bool management_sleep_task(void)
{
  if (!tud_mounted())
  {
    if (session.active)
      disconnect_session();
    return false;
  }

  session.sleep_wake_requested = false;
  session.processing_sleep_messages = true;
  management_transport_drain();
  management_transport_receive(dispatch, &session);
  session.processing_sleep_messages = false;
  management_transport_drain();
  return session.sleep_wake_requested;
}
