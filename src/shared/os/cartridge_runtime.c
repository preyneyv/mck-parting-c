#include <prism/runtime.h>
#include <prism/cartridge_identity.h>

#include <stdlib.h>
#include <string.h>

#include <platform/display.h>
#include <platform/cartridge.h>
#include <platform/persistence.h>
#include <platform/peripheral.h>
#include <platform/time.h>
#include <platform/system.h>
#include <shared/anim.h>
#include <shared/engine.h>
#include <shared/leaderboard/leaderboard.h>

static const prism_cartridge_t *current;
static const prism_cartridge_t *pending;
static void *pending_state;
static void *pending_persistent;
static platform_cartridge_execution_t current_execution;
static platform_cartridge_execution_t pending_execution;
static prism_t context;
static bool persistent_dirty;
static platform_time_t persistent_dirty_at;
static bool persistence_deferred;

static bool api_button_pressed(prism_button_t button)
{
  return engine_button_pressed((button_id_t)button);
}

static bool api_button_edge(prism_button_t button)
{
  return engine_button_app_keydown((button_id_t)button) ||
         engine_button_app_keyup((button_id_t)button);
}

static bool api_button_keydown(prism_button_t button)
{
  return engine_button_app_keydown((button_id_t)button);
}

static bool api_button_keyup(prism_button_t button)
{
  return engine_button_app_keyup((button_id_t)button);
}

static uint32_t api_button_keydown_tick(prism_button_t button)
{
  return engine_button_app_keydown_tick((button_id_t)button);
}

static uint32_t api_button_keyup_tick(prism_button_t button)
{
  return engine_button_app_keyup_tick((button_id_t)button);
}

static float api_button_hold_ratio(prism_button_t button)
{
  return engine_button_held_ratio((button_id_t)button);
}

static uint32_t api_ticks(void) { return engine_ticks(); }
static audio_synth_t *api_synth(void) { return engine_synth(); }

static void api_led_set(uint8_t led, color_t color)
{
  engine_led_set(led, color);
}

static void api_persist(void)
{
  if (context.persistent == NULL)
    return;
  persistent_dirty = true;
  persistent_dirty_at = platform_now_us();
}

static const prism_api_v1_t api_v1 = {
    .abi_version = PRISM_API_ABI_VERSION,
    .struct_size = sizeof(prism_api_v1_t),
    .display = platform_display_get_u8g2,
    .ticks = api_ticks,
    .now_us = platform_now_us,
    .time_diff_us = platform_time_diff_us,
    .button_pressed = api_button_pressed,
    .button_edge = api_button_edge,
    .button_hold_ratio = api_button_hold_ratio,
    .buttons_reset = engine_buttons_reset,
    .led_set = api_led_set,
    .synth = api_synth,
    .power_state = platform_peripheral_get_power_state,
    .sleep = engine_enter_sleep,
    .system_reset = platform_system_reset,
    .persist = api_persist,
    .keep_awake = engine_mark_input,
    .anim_to = anim_to,
    .anim_cancel = anim_cancel,
    .leaderboard_qrcode = leaderboard_get_qrcode,
    .button_keydown = api_button_keydown,
    .button_keyup = api_button_keyup,
    .button_keydown_tick = api_button_keydown_tick,
    .button_keyup_tick = api_button_keyup_tick,
};

static void adapter_enter(void)
{
  current = pending;
  pending = NULL;
  if (current == NULL)
    return;
  context.api = &api_v1;
  context.cartridge = current;
  context.state_size = current->state_size;
  context.state = pending_state;
  pending_state = NULL;
  context.persistent_size = current->persistent_size;
  context.persistent = pending_persistent;
  pending_persistent = NULL;
  current_execution = pending_execution;
  memset(&pending_execution, 0, sizeof(pending_execution));
  persistent_dirty = false;
  if (current->enter != NULL)
    platform_cartridge_call(&current_execution, current->enter, &context);
}

static void adapter_tick(void)
{
  if (current != NULL && current->tick != NULL)
    platform_cartridge_call(&current_execution, current->tick, &context);
}

static void adapter_frame(void)
{
  if (current != NULL && current->frame != NULL)
    platform_cartridge_call(&current_execution, current->frame, &context);
}

static void adapter_pause(void)
{
  if (current != NULL && current->pause != NULL)
    platform_cartridge_call(&current_execution, current->pause, &context);
}

static void adapter_resume(void)
{
  if (current != NULL && current->resume != NULL)
    platform_cartridge_call(&current_execution, current->resume, &context);
}

static void adapter_leave(void)
{
  if (current != NULL && current->leave != NULL)
    platform_cartridge_call(&current_execution, current->leave, &context);
  prism_cartridge_persistence_flush();
  free(context.state);
  free(context.persistent);
  platform_cartridge_release(&current_execution);
  memset(&context, 0, sizeof(context));
  current = NULL;
}

static app_t adapter = {
    .name = "cartridge",
    .enter = adapter_enter,
    .tick = adapter_tick,
    .frame = adapter_frame,
    .pause = adapter_pause,
    .resume = adapter_resume,
    .leave = adapter_leave,
};

bool prism_cartridge_launch(const prism_cartridge_t *cartridge)
{
  if (cartridge == NULL || cartridge->magic != PRISM_CARTRIDGE_MAGIC ||
      cartridge->abi_version != PRISM_CARTRIDGE_ABI_VERSION ||
      cartridge->descriptor_size < sizeof(prism_cartridge_t) ||
      !prism_cartridge_id_valid(cartridge->id) ||
      cartridge->name == NULL || cartridge->name[0] == '\0' ||
      cartridge->icon == NULL || cartridge->frame == NULL)
    return false;

  void *state = NULL;
  void *persistent = NULL;
  platform_cartridge_execution_t execution = {0};
  if (!platform_cartridge_prepare(cartridge, &execution))
    return false;
  if (cartridge->state_size > 0)
  {
    state = calloc(1, cartridge->state_size);
    if (state == NULL)
    {
      platform_cartridge_release(&execution);
      return false;
    }
  }

  if (cartridge->persistent_size > 0)
  {
    persistent = calloc(1, cartridge->persistent_size);
    if (persistent == NULL)
    {
      free(state);
      platform_cartridge_release(&execution);
      return false;
    }
    prism_app_key_t app_key;
    if (!prism_app_key_derive(cartridge->id, app_key))
    {
      free(persistent);
      free(state);
      platform_cartridge_release(&execution);
      return false;
    }
    platform_cartridge_data_load(app_key, cartridge->persistent_schema_version,
                                 persistent, cartridge->persistent_size);
  }

  pending = cartridge;
  pending_state = state;
  pending_persistent = persistent;
  pending_execution = execution;
  memset(adapter.name, 0, sizeof(adapter.name));
  strncpy(adapter.name, cartridge->name, sizeof(adapter.name) - 1);
  adapter.icon = cartridge->icon;
  adapter.tick_divider = cartridge->tick_divider == 0
                             ? 1u
                             : cartridge->tick_divider;
  engine_set_app(&adapter);
  return true;
}

const prism_cartridge_t *prism_cartridge_current(void) { return current; }
const prism_api_v1_t *prism_os_api(void) { return &api_v1; }

void prism_cartridge_persistence_flush(void)
{
  if (!persistent_dirty || current == NULL || context.persistent == NULL)
    return;
  prism_app_key_t app_key;
  if (prism_app_key_derive(current->id, app_key) &&
      platform_cartridge_data_save(app_key,
                                   current->persistent_schema_version,
                                   context.persistent,
                                   context.persistent_size))
    persistent_dirty = false;
}

void prism_cartridge_persistence_task(void)
{
  if (persistent_dirty && !persistence_deferred &&
      platform_time_diff_us(persistent_dirty_at, platform_now_us()) >=
          10000000)
    prism_cartridge_persistence_flush();
}

void prism_cartridge_persistence_set_deferred(bool deferred)
{
  persistence_deferred = deferred;
}
