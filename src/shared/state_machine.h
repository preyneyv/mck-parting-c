#pragma once

#include <pico/stdlib.h>
#include <stdint.h>

typedef struct {
  bool pressed;
  uint
} button_t;

typedef struct {
  button_t l;
  button_t r;

} button_state_t;

typedef struct {
  button_state_t buttons;
} state_machine_t;

void state_machine_init(state_machine_t *sm);
void state_machine_tick(state_machine_t *sm);
void state_machine_wait_for_frame(state_machine_t *sm);
void state_machine_handle_event(state_machine_t *sm, int event);
void state_machine_swap(state_machine_t *sm);
