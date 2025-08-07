#pragma once

#include <pico/time.h>

#include <shared/utils/q1x15.h>

#include "audio/synth.h"
#include "display.h"
#include "leds.h"
#include "peripheral.h"

typedef struct {
  // called when scene is entered. called after exit of previous scene.
  void (*enter)(void);
  // called every tick
  void (*update)(void);
  // called every frame
  void (*draw)(void);
  // called when scene is paused (sleep or menu)
  void (*pause)(void);
  // called when scene is resumed.
  void (*resume)(void);
  // called when scene is exited. called before enter of next scene.
  void (*exit)(void);
} scene_t;

typedef enum {
  BUTTON_LEFT,
  BUTTON_RIGHT,
  BUTTON_MENU,
} button_id_t;

typedef struct {
  button_id_t id;
  absolute_time_t pressed_at;
  bool pressed; // true if button is currently pressed
  bool evt;     // true if button was just transitioned this frame
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
} engine_t;

extern engine_t engine;
void engine_init();
void engine_run_forever();

void engine_buttons_init();
bool engine_button_read(button_id_t button_id);
