#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <platform/leds.h>
#include <platform/time.h>

#include <shared/utils/q1x15.h>

#include "audio/playback.h"
#include "audio/synth.h"

typedef struct
{
  char name[32];
  const uint8_t *icon;
  uint32_t tick_divider;
  void (*enter)(void);
  void (*tick)(void);
  void (*frame)(void);
  void (*pause)(void);
  void (*resume)(void);
  void (*leave)(void);
} app_t;

typedef enum
{
  BUTTON_NONE = 0,
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_MENU,
} button_id_t;

typedef struct
{
  button_id_t id;
  platform_time_t pressed_at;
  bool pressed;
  bool ignore;
  bool keydown;
  bool keyup;
  bool app_keydown;
  bool app_keyup;
  uint32_t app_keydown_tick;
  uint32_t app_keyup_tick;
} button_t;

void engine_init(void);
void engine_run_forever(void);
void engine_set_app(app_t *app);
void engine_request_home(void);
bool engine_is_app(const app_t *app);
void engine_buttons_reset(void);
void engine_enter_sleep(void);
void engine_wake(void);
void engine_pause(bool skip_animation);
void engine_resume(void);
void engine_set_volume(int8_t level);
void engine_change_volume(int8_t direction);
uint8_t engine_volume(void);
void engine_set_brightness(int8_t level);
void engine_change_brightness(int8_t direction);
uint8_t engine_brightness(void);
uint8_t engine_brightness_scale(void);
uint8_t engine_output_brightness_scale(void);
uint32_t engine_ticks(void);
platform_time_t engine_now(void);
audio_synth_t *engine_synth(void);
void engine_led_set(uint8_t led, color_t color);
color_t engine_led_color(uint8_t led);
void engine_mark_input(void);
void engine_set_frame_callback(void (*callback)(void));
void engine_set_tick_callback(void (*callback)(void));

bool engine_button_keydown(button_id_t button_id);
bool engine_button_keyup(button_id_t button_id);
bool engine_button_pressed(button_id_t button_id);
bool engine_button_edge(button_id_t button_id);
bool engine_button_app_keydown(button_id_t button_id);
bool engine_button_app_keyup(button_id_t button_id);
uint32_t engine_button_app_keydown_tick(button_id_t button_id);
uint32_t engine_button_app_keyup_tick(button_id_t button_id);
bool engine_button_released(button_id_t button_id);
button_id_t engine_button_get_pressed_first(void);
float engine_button_held_ratio(button_id_t button_id);

#define BUTTON_KEYDOWN(button_id) engine_button_keydown(button_id)
#define BUTTON_KEYUP(button_id) engine_button_keyup(button_id)
#define BUTTON_PRESSED(button_id) engine_button_pressed(button_id)
#define BUTTON_RELEASED(button_id) engine_button_released(button_id)

typedef struct
{
  const char *name;
  void (*action)(void);
} menu_action_t;
