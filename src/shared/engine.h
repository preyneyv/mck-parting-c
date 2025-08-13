#pragma once

#include <pico/time.h>

#include <shared/utils/q1x15.h>

#include "audio/playback.h"
#include "audio/synth.h"
#include "display.h"
#include "leds.h"
#include "peripheral.h"

typedef struct {
  char name[32]; // app name

  // called when scene is entered. called after exit of previous scene.
  void (*enter)(void);
  // called every tick
  void (*tick)(void);
  // called every frame
  void (*frame)(void);
  // called when scene is paused (sleep or menu)
  void (*pause)(void);
  // called when scene is resumed.
  void (*resume)(void);
  // called when scene is leave. called before enter of next scene.
  void (*leave)(void);
} engine_app_t;

typedef enum {
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_MENU,
} button_id_t;

typedef struct {
  button_id_t id;
  absolute_time_t pressed_at;
  bool pressed; // true if button is currently pressed
  bool edge;    // true if button was just transitioned this frame. will
                // automatically be reset next tick or frame
} button_t;

typedef struct {
  audio_synth_t synth;
  display_t display;
  leds_t leds;
  peripheral_t peripheral;
  struct {
    button_t left;
    button_t right;
    button_t menu;
  } buttons;

  absolute_time_t now;
  uint32_t tick;

  engine_app_t *app;
} engine_t;

extern engine_t g_engine;
void engine_init();
void engine_run_forever();
void engine_set_app(engine_app_t *app);
void engine_buttons_init();
bool engine_button_read(button_id_t button_id);
void engine_enter_sleep();           // todo: finalize api
void engine_sleep_until_interrupt(); // implement

static inline button_t *engine_button_from_id(button_id_t button_id) {
  switch (button_id) {
  case BUTTON_LEFT:
    return &g_engine.buttons.left;
  case BUTTON_RIGHT:
    return &g_engine.buttons.right;
  case BUTTON_MENU:
    return &g_engine.buttons.menu;
  default:
    return NULL;
  }
}

static inline bool engine_button_keydown(button_id_t button_id) {
  button_t *button = engine_button_from_id(button_id);
  return button->pressed && button->edge;
}

static inline bool engine_button_keyup(button_id_t button_id) {
  button_t *button = engine_button_from_id(button_id);
  return !button->pressed && button->edge;
}

static inline bool engine_button_pressed(button_id_t button_id) {
  button_t *button = engine_button_from_id(button_id);
  return button->pressed;
}

static inline bool engine_button_released(button_id_t button_id) {
  button_t *button = engine_button_from_id(button_id);
  return !button->pressed;
}

#define BUTTON_KEYDOWN(button_id) engine_button_keydown(button_id)
#define BUTTON_KEYUP(button_id) engine_button_keyup(button_id)
#define BUTTON_PRESSED(button_id) engine_button_pressed(button_id)
#define BUTTON_RELEASED(button_id) engine_button_released(button_id)
