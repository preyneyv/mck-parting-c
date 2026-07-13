#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <shared/config.h>
#include <platform/display.h>
#include <platform/input.h>
#include <platform/peripheral.h>
#include <platform/platform.h>
#include <platform/sleep.h>
#include <platform/system.h>
#include <platform/time.h>
#include <prism/graphics/color.h>
#include <prism/graphics/layout.h>
#include <prism/graphics/vector.h>
#include <shared/utils/misc.h>
#if PRISM_ENABLE_PERFORMANCE_LOGS
#include <shared/utils/timing.h>
#endif

#include "anim.h"
#include "audio/synth_internal.h"
#include "os/engine_internal.h"
#include "os/launcher.h"
#include "os/pause_menu.h"
#include "os/settings.h"
#include "os/wake_confirmation.h"
#include <prism/runtime.h>
#include "engine.h"

typedef struct
{
  void (*on_frame)(void);
  void (*on_tick)(void);
  audio_synth_t synth;
  color_t led_colors[LED_COUNT];
  struct
  {
    button_t left;
    button_t right;
    button_t menu;
  } buttons;
  platform_time_t now;
  platform_time_t next_tick_at;
  platform_time_t next_frame_at;
  platform_time_t last_input_at;
  uint32_t tick;
  uint32_t app_tick_phase;
  uint32_t tick_deadline_remainder;
  uint32_t frame_deadline_remainder;
  app_t *app;
  uint8_t volume;
  uint8_t brightness;
  bool paused;
} engine_t;

static engine_t g_engine;
static uint8_t last_app_framebuffer[(DISP_WIDTH * DISP_HEIGHT) / 8];

#if PRISM_ENABLE_PERFORMANCE_LOGS
static TimingInstrumenter engine_tick_timing;
static TimingInstrumenter app_tick_timing;
static TimingInstrumenter frame_timing;
#endif

enum
{
  ENGINE_TRACE_APP_TICK = 0x5449434bu,
  ENGINE_TRACE_APP_FRAME = 0x4652414du,
  ENGINE_TRACE_TICK_CALLBACK = 0x54484f4bu,
  ENGINE_TRACE_FRAME_CALLBACK = 0x484f4f4bu,
  ENGINE_TRACE_DISPLAY = 0x44495350u,
  ENGINE_TRACE_AUDIO_RESET = 0x41525354u,
  MAX_TICKS_PER_LOOP = 8,
};

typedef struct
{
  uint32_t stage;
  uint32_t detail;
} watchdog_trace_t;

static watchdog_trace_t trace_begin(uint32_t stage, const void *detail)
{
  watchdog_trace_t previous = {
      .stage = platform_watchdog_trace_stage(),
      .detail = platform_watchdog_trace_detail(),
  };
  platform_watchdog_trace(stage, (uint32_t)(uintptr_t)detail);
  return previous;
}

static void trace_end(watchdog_trace_t previous)
{
  platform_watchdog_trace(previous.stage, previous.detail);
}

static button_t *button_from_id(button_id_t id)
{
  switch (id)
  {
  case BUTTON_LEFT: return &g_engine.buttons.left;
  case BUTTON_RIGHT: return &g_engine.buttons.right;
  case BUTTON_MENU: return &g_engine.buttons.menu;
  default: return NULL;
  }
}

bool engine_button_keydown(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL && button->keydown;
}

bool engine_button_keyup(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL && button->keyup;
}

bool engine_button_pressed(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL && button->pressed;
}

bool engine_button_edge(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL && (button->keydown || button->keyup);
}

bool engine_button_app_keydown(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL && button->app_keydown;
}

bool engine_button_app_keyup(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL && button->app_keyup;
}

uint32_t engine_button_app_keydown_tick(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL ? button->app_keydown_tick : 0;
}

uint32_t engine_button_app_keyup_tick(button_id_t id)
{
  button_t *button = button_from_id(id);
  return button != NULL ? button->app_keyup_tick : 0;
}

bool engine_button_released(button_id_t id)
{
  return !engine_button_pressed(id);
}

button_id_t engine_button_get_pressed_first(void)
{
  button_t *left = button_from_id(BUTTON_LEFT);
  button_t *right = button_from_id(BUTTON_RIGHT);
  if (left->pressed && right->pressed)
    return left->pressed_at <= right->pressed_at ? BUTTON_LEFT : BUTTON_RIGHT;
  if (left->pressed)
    return BUTTON_LEFT;
  if (right->pressed)
    return BUTTON_RIGHT;
  return BUTTON_NONE;
}

float engine_button_held_ratio(button_id_t id)
{
  enum { HOLD_START_MS = 300, HOLD_CONFIRM_MS = 1200 };
  button_t *button = button_from_id(id);
  if (button == NULL || !button->pressed)
    return 0.f;
  int32_t held_ms =
      (int32_t)(platform_time_diff_us(button->pressed_at, g_engine.now) / 1000);
  float ratio = (held_ms - HOLD_START_MS) /
                (float)(HOLD_CONFIRM_MS - HOLD_START_MS);
  if (ratio < 0.f)
    return 0.f;
  if (ratio > 1.f)
    return 1.f;
  return ratio;
}

uint32_t engine_ticks(void) { return g_engine.tick; }
platform_time_t engine_now(void) { return g_engine.now; }
audio_synth_t *engine_synth(void) { return &g_engine.synth; }
uint8_t engine_volume(void) { return g_engine.volume; }
uint8_t engine_brightness(void) { return g_engine.brightness; }
bool engine_is_app(const app_t *app) { return g_engine.app == app; }

void engine_set_frame_callback(void (*callback)(void))
{
  g_engine.on_frame = callback;
}

void engine_set_tick_callback(void (*callback)(void))
{
  g_engine.on_tick = callback;
}

void engine_led_set(uint8_t led, color_t color)
{
  if (led < LED_COUNT)
    g_engine.led_colors[led] = color;
}

// log-inspired volume curve for more perceptually linear volume steps.
static const q1x15 ENGINE_VOLUME_CURVE[9] = {
    Q1X15_ZERO, // mute
    2195,       // ~0.067
    5406,       // ~0.165
    9142,       // ~0.279
    13311,      // ~0.406
    17797,      // ~0.543
    22534,      // ~0.688
    27557,      // ~0.841
    Q1X15_ONE,  // full scale
};

void engine_init()
{
  // initialize all subsystems
  audio_synth_init(&g_engine.synth, AUDIO_SAMPLE_RATE, 1000);
  for (uint8_t i = 0; i < LED_COUNT; i++)
  {
    g_engine.led_colors[i] = (color_t){.hex = 0x000000};
  }

  g_engine.buttons.left.id = BUTTON_LEFT;
  g_engine.buttons.right.id = BUTTON_RIGHT;
  g_engine.buttons.menu.id = BUTTON_MENU;

  prism_settings_init();
  engine_set_app(NULL);
  engine_set_volume((int8_t)prism_settings_get()->volume);
  engine_set_brightness((int8_t)prism_settings_get()->brightness);
  g_engine.last_input_at = platform_now_us();
}

void engine_set_volume(int8_t level)
{
  int clamped = level;
  if (clamped < 0)
    clamped = 0;
  if (clamped > 8)
    clamped = 8;
  g_engine.volume = (uint8_t)clamped;
  audio_synth_set_master_level(&g_engine.synth,
                               ENGINE_VOLUME_CURVE[g_engine.volume]);
}

inline void engine_change_volume(int8_t direction)
{
  engine_set_volume(g_engine.volume + direction);
  prism_settings_volume_changed(g_engine.volume);
}

uint8_t engine_brightness_scale(void)
{
  /* Perceptual output curve.  Low settings get finer control while the larger
   * top-end jumps keep 6/8, 7/8, and 8/8 visually distinct.  The minimum is
   * deliberately the old linear 1/8 value. */
  static const uint8_t brightness_curve[9] = {
      60, 68, 80, 96, 116, 142, 174, 212, 255,
  };
  return brightness_curve[g_engine.brightness];
}

uint8_t engine_output_brightness_scale(void)
{
  uint8_t configured = engine_brightness_scale();
  int64_t idle_us = platform_time_diff_us(g_engine.last_input_at,
                                          platform_now_us());
  const int64_t sleep_us = (int64_t)AUTO_SLEEP_TIMEOUT_MS * 1000;
  const int64_t fade_us = (int64_t)AUTO_SLEEP_FADE_MS * 1000;
  if (idle_us <= sleep_us - fade_us)
    return configured;
  if (idle_us >= sleep_us)
    return 0;
  int64_t remaining_us = sleep_us - idle_us;
  return (uint8_t)(((uint32_t)configured * (uint32_t)remaining_us) /
                   (uint32_t)fade_us);
}

void engine_set_brightness(int8_t level)
{
  int clamped = level;
  if (clamped < 0)
    clamped = 0;
  if (clamped > 8)
    clamped = 8;
  g_engine.brightness = (uint8_t)clamped;
  platform_display_set_contrast(engine_brightness_scale());
  platform_leds_set_brightness(engine_brightness_scale());
}

void engine_change_brightness(int8_t direction)
{
  engine_set_brightness(g_engine.brightness + direction);
  prism_settings_brightness_changed(g_engine.brightness);
}

color_t engine_led_color(uint8_t led)
{
  if (led >= LED_COUNT)
    return (color_t){.hex = 0};
  return wake_confirmation_blend_led(led, g_engine.led_colors[led]);
}

void engine_mark_input()
{
  g_engine.last_input_at = platform_now_us();
}

static void read_button(button_t *button, platform_time_t now, bool pressed)
{
  if (pressed)
  {
    if (!button->pressed && !button->ignore)
    {
      button->pressed = true;
      button->pressed_at = now;
      button->keydown = true;
      button->app_keydown = true;
      button->app_keydown_tick = g_engine.tick;
    }
  }
  else
  {
    button->ignore = false;
    if (button->pressed)
    {
      // released
      button->pressed = false;
      button->pressed_at = PLATFORM_TIME_ZERO;
      button->keyup = true;
      button->app_keyup = true;
      button->app_keyup_tick = g_engine.tick;
    }
  }
  if (button->keydown || button->keyup)
  {
    engine_mark_input();
  }
}

static void clear_system_button_edges(void)
{
  g_engine.buttons.left.keydown = false;
  g_engine.buttons.left.keyup = false;
  g_engine.buttons.right.keydown = false;
  g_engine.buttons.right.keyup = false;
  g_engine.buttons.menu.keydown = false;
  g_engine.buttons.menu.keyup = false;
}

static void clear_app_button_edges(void)
{
  g_engine.buttons.left.app_keydown = false;
  g_engine.buttons.left.app_keyup = false;
  g_engine.buttons.right.app_keydown = false;
  g_engine.buttons.right.app_keyup = false;
  g_engine.buttons.menu.app_keydown = false;
  g_engine.buttons.menu.app_keyup = false;
}

static void reset_button(button_t *button, bool ignore)
{
  button->pressed = false;
  button->pressed_at = PLATFORM_TIME_ZERO;
  button->ignore = ignore;
  button->keydown = false;
  button->keyup = false;
  button->app_keydown = false;
  button->app_keyup = false;
  button->app_keydown_tick = 0;
  button->app_keyup_tick = 0;
}

void engine_buttons_reset()
{
  reset_button(&g_engine.buttons.left, true);
  reset_button(&g_engine.buttons.right, true);
}

static void reset_buttons(bool ignore_menu)
{
  engine_buttons_reset();

  if (ignore_menu)
  {
    reset_button(&g_engine.buttons.menu, true);
  }
}

static void handle_menu_reset()
{
  // if the menu button is held down for a while, reset using watchdog
  platform_watchdog_update();
  if (g_engine.buttons.menu.pressed)
  {
    if (platform_time_reached(
            platform_time_add_ms(g_engine.buttons.menu.pressed_at, 5000)))
    {
      prism_settings_flush();
      // reset the system
      platform_system_reset();
      while (1)
        ;
    }
  }
}

static platform_wake_result_t engine_sleep_cycle(uint32_t quick_wake_ms)
{
  prism_settings_flush();
  platform_watchdog_disable();
  const color_t leds_off[LED_COUNT] = {0};

  /* platform_leds_show() queues the WS2812 words, but the RP2 PIO still needs
   * time to shift them out and hold the data line low long enough to latch.
   * Entering dormant sleep immediately can suspend that transfer mid-frame,
   * which briefly restores stale LED data when the clocks restart. */
  platform_leds_show(leds_off, LED_COUNT);
  platform_sleep_us(200);
  platform_display_set_enabled(false);
  platform_peripheral_set_enabled(false);
  audio_playback_set_enabled(false);
  platform_wake_result_t wake = platform_sleep_enter(quick_wake_ms);

  audio_playback_set_enabled(true);
  platform_watchdog_enable(WATCHDOG_TIMEOUT_MS);
  /* Restore the configured level before powering the OLED back up so wake
   * never exposes the last zero-contrast fade value. */
  platform_peripheral_set_enabled(true);
  platform_peripheral_read_inputs();
  platform_display_set_contrast(engine_brightness_scale());
  platform_display_set_enabled(true);

  reset_buttons(true); // ignore any edges from waking up
  engine_mark_input(); // mark input to avoid auto-sleep right after waking up
  g_engine.next_tick_at = platform_now_us();
  g_engine.next_frame_at = platform_now_us();
  g_engine.tick_deadline_remainder = 0;
  g_engine.frame_deadline_remainder = 0;
  return wake;
}

static void resume_after_auto_sleep(void);

void engine_enter_sleep()
{
  platform_wake_result_t wake = engine_sleep_cycle(0);
  if (wake.confirmation_required && wake.wake_button != 0)
    wake_confirmation_start(wake.wake_button);
  else
    resume_after_auto_sleep();
}

bool engine_is_paused(void) { return g_engine.paused; }

const char *engine_app_name(void) { return g_engine.app->name; }

static void resume_after_auto_sleep(void)
{
  wake_confirmation_cancel();
  pause_menu_hide_immediate();
  anim_sys_set_paused(false);
  if (g_engine.paused)
  {
    g_engine.paused = false;
    if (g_engine.app != NULL && g_engine.app->resume != NULL)
      g_engine.app->resume();
  }
  reset_buttons(true);
  engine_mark_input();
}

void engine_finish_wake(void) { resume_after_auto_sleep(); }

void engine_wake(void)
{
  if (wake_confirmation_active())
    resume_after_auto_sleep();
}

static void engine_enter_auto_sleep(void)
{
  engine_pause(true);
  platform_wake_result_t wake = engine_sleep_cycle(2000);
  if (wake.confirmation_required && wake.wake_button != 0)
    wake_confirmation_start(wake.wake_button);
  else
    resume_after_auto_sleep();
}

static void wake_confirmation_timeout(void)
{
  platform_wake_result_t wake = engine_sleep_cycle(0);
  if (wake.confirmation_required && wake.wake_button != 0)
    wake_confirmation_start(wake.wake_button);
  else
    resume_after_auto_sleep();
}

static uint32_t app_tick_divider(void)
{
  return g_engine.app != NULL && g_engine.app->tick_divider != 0
             ? g_engine.app->tick_divider
             : 1u;
}

static inline void engine_do_tick(uint32_t elapsed_us)
{
  anim_tick_us(elapsed_us); // system animations continue while apps are paused
  if (wake_confirmation_active())
  {
    wake_confirmation_tick();
    /* Wake confirmation runs at the 960 Hz engine rate, while ordinary edges
     * are normally retired by the 120 Hz frame loop. Consume them here so a
     * single physical press can never be counted more than once. */
    clear_system_button_edges();
    clear_app_button_edges();
  }
  else if (!g_engine.paused)
  {
    g_engine.tick++;
    g_engine.app_tick_phase++;
    if (g_engine.app_tick_phase >= app_tick_divider())
    {
      g_engine.app_tick_phase = 0;
      if (g_engine.app->tick != NULL)
      {
        watchdog_trace_t trace =
            trace_begin(ENGINE_TRACE_APP_TICK, g_engine.app);
#if PRISM_ENABLE_PERFORMANCE_LOGS
        ti_start(&app_tick_timing);
#endif
        g_engine.app->tick();
#if PRISM_ENABLE_PERFORMANCE_LOGS
        ti_stop(&app_tick_timing);
#endif
        trace_end(trace);
      }
      clear_app_button_edges();
    }
  }

  if (g_engine.on_tick != NULL)
  {
    watchdog_trace_t trace =
        trace_begin(ENGINE_TRACE_TICK_CALLBACK, g_engine.app);
    g_engine.on_tick();
    trace_end(trace);
  }
}

static inline void engine_do_frame()
{
  for (uint8_t i = 0; i < LED_COUNT; i++)
  {
    g_engine.led_colors[i] = (color_t){.hex = 0x000000};
  }
  if (wake_confirmation_expired())
    wake_confirmation_timeout();

  if (wake_confirmation_active())
  {
    wake_confirmation_frame(last_app_framebuffer);
  }
  else if (!g_engine.paused)
  {
    // draw screen buffer
    u8g2_t *u8g2 = platform_display_get_u8g2();
    u8g2_ClearBuffer(u8g2);
    if (g_engine.app->frame != NULL)
    {
      watchdog_trace_t trace =
          trace_begin(ENGINE_TRACE_APP_FRAME, g_engine.app);
      g_engine.app->frame();
      trace_end(trace);
    }
    memcpy(last_app_framebuffer, u8g2_GetBufferPtr(u8g2),
           sizeof(last_app_framebuffer));
  }

  if (!wake_confirmation_active())
  {
    pause_menu_frame();
    /* Manual sleep blocks inside pause_menu_frame() until a wake input
     * arrives. If that starts wake confirmation, replace the already-buffered
     * pause UI and its LED state in this same render pass instead of exposing
     * either for one frame. */
    if (wake_confirmation_active())
      wake_confirmation_frame(last_app_framebuffer);
    else
      prism_settings_frame();
  }
  prism_settings_task();
  prism_cartridge_persistence_task();

  if (g_engine.on_frame != NULL)
  {
    watchdog_trace_t trace =
        trace_begin(ENGINE_TRACE_FRAME_CALLBACK, g_engine.app);
    g_engine.on_frame();
    trace_end(trace);
  }

  // Apply the final OS-owned idle fade after the app and menu have rendered.
  platform_display_set_contrast(engine_output_brightness_scale());
  // write display
  watchdog_trace_t display_trace =
      trace_begin(ENGINE_TRACE_DISPLAY, g_engine.app);
  u8g2_SendBuffer(platform_display_get_u8g2());
  trace_end(display_trace);
  // write LEDs
  color_t output_leds[LED_COUNT];
  for (uint8_t i = 0; i < LED_COUNT; ++i)
    output_leds[i] = engine_led_color(i);
  platform_leds_set_brightness(engine_output_brightness_scale());
  platform_leds_show(output_leds, LED_COUNT);
  wake_confirmation_release_tick();

  // System UI edges last for a single rendered frame. Cartridge edges have a
  // separate latch and remain pending until that cartridge's divided tick.
  clear_system_button_edges();

  // check for auto sleep
  if (!wake_confirmation_active() && platform_time_reached(
          platform_time_add_ms(g_engine.last_input_at, AUTO_SLEEP_TIMEOUT_MS)))
  {
    engine_enter_auto_sleep();
  }
}

void engine_run_forever()
{
  platform_display_set_enabled(true);
  platform_peripheral_set_enabled(true);
  platform_task();

#if PRISM_ENABLE_PERFORMANCE_LOGS
  ti_init(&engine_tick_timing);
  ti_init(&app_tick_timing);
  ti_init(&frame_timing);
  platform_time_t last_log_us = platform_now_us();
#endif

  platform_watchdog_enable(WATCHDOG_TIMEOUT_MS);
  g_engine.now = PLATFORM_TIME_ZERO;
  g_engine.tick = 0;
  g_engine.app_tick_phase = 0;

  g_engine.next_tick_at = platform_now_us();
  g_engine.next_frame_at = platform_now_us();
  g_engine.tick_deadline_remainder = 0;
  g_engine.frame_deadline_remainder = 0;

  while (true)
  {
    platform_watchdog_update();
    platform_task();

    platform_time_t now = platform_now_us();
    int ticks_processed = 0;
    while (now >= g_engine.next_tick_at &&
           ticks_processed < MAX_TICKS_PER_LOOP)
    {
#if PRISM_ENABLE_PERFORMANCE_LOGS
      ti_start(&engine_tick_timing);
#endif

      g_engine.now = g_engine.next_tick_at;

      if (ticks_processed == 0)
      {
        platform_input_mask_t input_mask = platform_input_read_mask();
        read_button(&g_engine.buttons.left, g_engine.now, (input_mask & PLATFORM_INPUT_LEFT) != 0);
        read_button(&g_engine.buttons.right, g_engine.now, (input_mask & PLATFORM_INPUT_RIGHT) != 0);
        read_button(&g_engine.buttons.menu, g_engine.now, (input_mask & PLATFORM_INPUT_MENU) != 0);

        handle_menu_reset();
      }

      uint32_t tick_elapsed_us = 1000000u / PRISM_ENGINE_TICK_RATE;
      g_engine.tick_deadline_remainder +=
          1000000u % PRISM_ENGINE_TICK_RATE;
      if (g_engine.tick_deadline_remainder >= PRISM_ENGINE_TICK_RATE)
      {
        g_engine.tick_deadline_remainder -= PRISM_ENGINE_TICK_RATE;
        tick_elapsed_us++;
      }

      engine_do_tick(tick_elapsed_us);

      ++ticks_processed;

      g_engine.next_tick_at += tick_elapsed_us;
      now = platform_now_us();
#if PRISM_ENABLE_PERFORMANCE_LOGS
      ti_stop(&engine_tick_timing);
#endif
    }

    now = platform_now_us();
    if (platform_time_reached(g_engine.next_frame_at))
    {
      platform_time_t frame_started_at = now;
#if PRISM_ENABLE_PERFORMANCE_LOGS
      ti_start(&frame_timing);
#endif
      engine_do_frame();

      platform_time_t frame_finished_at = platform_now_us();
      uint32_t frame_interval_us = 1000000u / TARGET_FPS;
      g_engine.frame_deadline_remainder += 1000000u % TARGET_FPS;
      if (g_engine.frame_deadline_remainder >= TARGET_FPS)
      {
        g_engine.frame_deadline_remainder -= TARGET_FPS;
        frame_interval_us++;
      }
      uint64_t frame_time = frame_finished_at - frame_started_at;
      g_engine.next_frame_at = frame_time < frame_interval_us
                                   ? frame_started_at + frame_interval_us
                                   : frame_finished_at;
#if PRISM_ENABLE_PERFORMANCE_LOGS
      ti_stop(&frame_timing);
#endif
    }

#if PRISM_ENABLE_PERFORMANCE_LOGS
    now = platform_now_us();
    int32_t log_time = platform_time_diff_us(last_log_us, now);
    if (log_time > 1000000)
    {
      uint32_t fps =
          (uint32_t)(frame_timing.count / (log_time / 1000000.0f));
      uint32_t engine_tps =
          (uint32_t)(engine_tick_timing.count / (log_time / 1000000.0f));
      uint32_t app_tps =
          (uint32_t)(app_tick_timing.count / (log_time / 1000000.0f));

      float engine_tick_average_ms =
          ti_get_average_ms(&engine_tick_timing, true);
      float app_tick_average_ms = ti_get_average_ms(&app_tick_timing, true);
      float frame_average_ms = ti_get_average_ms(&frame_timing, true);
      printf(
          "fps: %" PRIu32 " | engine: %" PRIu32 " | app: %" PRIu32
          " | frame: %.2f / %.2f ms | app tick: %.2f ms | engine tick: %.2f ms\n",
          fps, engine_tps, app_tps,
          frame_average_ms, TARGET_FRAME_INTERVAL_US / 1000.0f,
          app_tick_average_ms, engine_tick_average_ms);

      last_log_us = now;
    }
#endif

    platform_time_t next_deadline = MIN(g_engine.next_tick_at, g_engine.next_frame_at);
    int64_t sleep_time = platform_time_diff_us(platform_now_us(), next_deadline);
    if (sleep_time > 0)
      platform_sleep_us(sleep_time);
  }
}

void engine_set_app(app_t *app)
{
  if (wake_confirmation_active())
    wake_confirmation_cancel();
  if (g_engine.app != NULL && g_engine.app->leave != NULL)
  {
    g_engine.app->leave();
  }
  anim_sys_clear_all();
  /* App switches can be driven asynchronously by USB MIDI and management.
   * Never leave a partially animated pause shade attached to the next app,
   * and do not carry a menu edge across the transition. */
  pause_menu_hide_immediate();
  reset_buttons(true);

  if (app == NULL)
  {
    app = &app_launcher;
  }

  g_engine.tick = 0;
  g_engine.app_tick_phase = 0;
  g_engine.app = app;
  g_engine.paused = false;
  anim_sys_set_paused(false);

  watchdog_trace_t audio_trace =
      trace_begin(ENGINE_TRACE_AUDIO_RESET, g_engine.app);
  audio_synth_reset(&g_engine.synth);
  trace_end(audio_trace);

  // seed based on time
  srand((unsigned int)platform_now_us());

  if (g_engine.app != NULL && g_engine.app->enter != NULL)
  {
    g_engine.app->enter();
  }
}

void engine_pause(bool skip_animation)
{
  if (g_engine.paused)
    return;
  reset_buttons(false);
  g_engine.paused = true;
  anim_sys_set_paused(true);
  if (g_engine.app != NULL && g_engine.app->pause != NULL)
  {
    g_engine.app->pause();
  }
  pause_menu_show(skip_animation);
}

void engine_resume()
{
  if (!g_engine.paused)
    return;
  reset_buttons(false);
  g_engine.paused = false;
  anim_sys_set_paused(false);
  if (g_engine.app != NULL && g_engine.app->resume != NULL)
  {
    g_engine.app->resume();
  }
  pause_menu_hide();
}
