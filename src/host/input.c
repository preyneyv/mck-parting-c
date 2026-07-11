#include <platform/input.h>

#include <SDL.h>
#include <stdlib.h>

void platform_input_init(void) {}

static platform_input_mask_t input_mask;
static platform_input_mask_t deferred_release_mask;
static platform_input_mask_t remote_mask;

static platform_input_mask_t mask_for_scancode(SDL_Scancode scancode) {
  switch (scancode) {
  case SDL_SCANCODE_LEFT:
  case SDL_SCANCODE_A:
    return PLATFORM_INPUT_LEFT;
  case SDL_SCANCODE_RIGHT:
  case SDL_SCANCODE_D:
    return PLATFORM_INPUT_RIGHT;
  case SDL_SCANCODE_ESCAPE:
  case SDL_SCANCODE_SPACE:
    return PLATFORM_INPUT_MENU;
  default:
    return 0;
  }
}

platform_input_mask_t platform_input_read_mask(void) {
  SDL_PumpEvents();

  // If a complete key tap arrived between engine ticks, keep it pressed for
  // this sample and release it on the next. Sampling SDL_GetKeyboardState()
  // directly loses those taps because both events have already completed.
  input_mask &= ~deferred_release_mask;
  deferred_release_mask = 0;

  platform_input_mask_t pressed_this_sample = 0;
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT)
      exit(0);
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
      continue;

    platform_input_mask_t bit = mask_for_scancode(event.key.keysym.scancode);
    if (bit == 0)
      continue;

    if (event.type == SDL_KEYDOWN) {
      input_mask |= bit;
      pressed_this_sample |= bit;
      deferred_release_mask &= ~bit;
    } else if ((pressed_this_sample & bit) != 0) {
      deferred_release_mask |= bit;
    } else {
      input_mask &= ~bit;
    }
  }

  return input_mask | remote_mask;
}

void platform_input_set_remote_mask(platform_input_mask_t mask) {
  remote_mask = mask &
                (PLATFORM_INPUT_LEFT | PLATFORM_INPUT_RIGHT | PLATFORM_INPUT_MENU);
}
