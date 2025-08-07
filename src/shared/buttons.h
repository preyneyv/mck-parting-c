#pragma once

#include <pico/time.h>
#include <shared/utils/q1x15.h>

typedef struct {
  absolute_time_t pressed_at;
  q1x15 held;

} button_t;

typedef struct {
  button_t left;
  button_t right;
  button_t menu;
} buttons_t;

void button_init(button_t *button);
void button_pressed(button_t *button);
void button_released(button_t *button);
void button_tick(button_t *button);

void buttons_init(buttons_t *buttons);
void buttons_tick(buttons_t *buttons);
void buttons_update(buttons_t *buttons);
