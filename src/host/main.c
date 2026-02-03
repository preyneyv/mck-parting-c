#include <pthread.h>
#include <stdlib.h>

#include <SDL.h>

#include <shared/engine.h>

#include "audio.h"

static bool button_states[4] = {false, false, false, false};

void engine_buttons_init() {
  for (size_t i = 0; i < sizeof(button_states) / sizeof(button_states[0]);
       i++) {
    button_states[i] = false;
  }
}

bool engine_button_read(button_id_t button_id) {
  return button_states[button_id];
}

static void handle_sdl_events() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_QUIT:
      exit(0);
      break;
    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      bool pressed = event.type == SDL_KEYDOWN;
      SDL_Keycode key = event.key.keysym.sym;
      if (key == SDLK_LEFT) {
        button_states[BUTTON_LEFT] = pressed;
      } else if (key == SDLK_RIGHT) {
        button_states[BUTTON_RIGHT] = pressed;
      } else if (key == SDLK_SPACE) {
        button_states[BUTTON_MENU] = pressed;
      }
      break;
    }
    default:
      break;
    }
  }
}

static void on_frame_cb() { handle_sdl_events(); }

void *audio_thread_main(void *arg) {
  audio_synth_t *synth = arg;
  audio_init(synth);
  return NULL;
}

int main() {
  engine_init();

  pthread_t audio_thread;
  pthread_create(&audio_thread, NULL, audio_thread_main, &g_engine.synth);

  g_engine.on_frame_cb = on_frame_cb;
  engine_run_forever();

  pthread_join(audio_thread, NULL);
}
