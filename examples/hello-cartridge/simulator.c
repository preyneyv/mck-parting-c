#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <prism/sdk.h>

extern const prism_cartridge_t cartridge_hello;

enum
{
  DISPLAY_WIDTH = 128,
  DISPLAY_HEIGHT = 64,
  DISPLAY_SCALE = 5,
  HOLD_MS = 800,
};

typedef struct
{
  bool pressed;
  bool keydown;
  bool keyup;
  bool ignored;
  uint32_t pressed_at;
  uint32_t keydown_at;
  uint32_t keyup_at;
} simulator_button_t;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static u8g2_t display;
static uint8_t display_buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
static uint32_t pixels[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static simulator_button_t buttons[PRISM_BUTTON_MENU + 1];
static uint32_t tick_count;
static bool running = true;

static const u8x8_display_info_t display_info = {
    .chip_enable_level = 0,
    .chip_disable_level = 1,
    .spi_mode = 1,
    .tile_width = DISPLAY_WIDTH / 8,
    .tile_height = DISPLAY_HEIGHT / 8,
    .pixel_width = DISPLAY_WIDTH,
    .pixel_height = DISPLAY_HEIGHT,
};

static uint8_t simulator_display_callback(u8x8_t *u8x8, uint8_t message,
                                          uint8_t argument, void *data)
{
  (void)argument;
  (void)data;
  if (message == U8X8_MSG_DISPLAY_SETUP_MEMORY)
  {
    u8x8_d_helper_display_setup_memory(u8x8, &display_info);
    return 1;
  }
  return 0;
}

static u8g2_t *simulator_display(void) { return &display; }
static uint32_t simulator_ticks(void) { return tick_count; }

static prism_time_t simulator_now_us(void)
{
  uint64_t counter = SDL_GetPerformanceCounter();
  return counter * 1000000u / SDL_GetPerformanceFrequency();
}

static int64_t simulator_time_diff_us(prism_time_t from, prism_time_t to)
{
  return (int64_t)(to - from);
}

static simulator_button_t *simulator_button(prism_button_t button)
{
  return button > PRISM_BUTTON_NONE && button <= PRISM_BUTTON_MENU
             ? &buttons[button]
             : NULL;
}

static bool simulator_button_pressed(prism_button_t button)
{
  simulator_button_t *value = simulator_button(button);
  return value != NULL && value->pressed;
}

static bool simulator_button_edge(prism_button_t button)
{
  simulator_button_t *value = simulator_button(button);
  return value != NULL && (value->keydown || value->keyup);
}

static bool simulator_button_keydown(prism_button_t button)
{
  simulator_button_t *value = simulator_button(button);
  return value != NULL && value->keydown;
}

static bool simulator_button_keyup(prism_button_t button)
{
  simulator_button_t *value = simulator_button(button);
  return value != NULL && value->keyup;
}

static uint32_t simulator_button_keydown_tick(prism_button_t button)
{
  simulator_button_t *value = simulator_button(button);
  return value != NULL ? value->keydown_at : 0;
}

static uint32_t simulator_button_keyup_tick(prism_button_t button)
{
  simulator_button_t *value = simulator_button(button);
  return value != NULL ? value->keyup_at : 0;
}

static float simulator_button_hold_ratio(prism_button_t button)
{
  simulator_button_t *value = simulator_button(button);
  if (value == NULL || !value->pressed)
    return 0.0f;
  uint32_t held_ms = prism_ms_from_ticks(tick_count - value->pressed_at);
  return held_ms >= HOLD_MS ? 1.0f : (float)held_ms / HOLD_MS;
}

static void simulator_buttons_reset(void)
{
  for (prism_button_t button = PRISM_BUTTON_LEFT;
       button <= PRISM_BUTTON_MENU; ++button)
  {
    buttons[button].pressed = false;
    buttons[button].keydown = false;
    buttons[button].keyup = false;
    buttons[button].ignored = true;
  }
}

static void simulator_led_set(uint8_t led, prism_color_t color)
{
  (void)led;
  (void)color;
}

static audio_synth_t *simulator_synth(void) { return NULL; }

static prism_power_state_t simulator_power_state(void)
{
  return (prism_power_state_t){.plugged_in = true, .battery_level = 100};
}

static void simulator_noop(void) {}

static void simulator_reset(void)
{
  running = false;
}

static int simulator_anim_to(volatile int32_t *subject, int32_t target,
                             uint32_t duration, prism_anim_ease_t easing,
                             prism_anim_done_fn callback, void *user)
{
  (void)duration;
  (void)easing;
  *subject = target;
  if (callback != NULL)
    callback(user);
  return 0;
}

static void simulator_anim_cancel(volatile int32_t *subject, int finish)
{
  (void)subject;
  (void)finish;
}

static bool simulator_leaderboard(uint8_t app_id, const void *data,
                                  size_t data_len,
                                  uint8_t *qrcode)
{
  (void)app_id;
  (void)data;
  (void)data_len;
  (void)qrcode;
  return false;
}

static const prism_api_v1_t simulator_api = {
    .abi_version = PRISM_API_ABI_VERSION,
    .struct_size = sizeof(simulator_api),
    .display = simulator_display,
    .ticks = simulator_ticks,
    .now_us = simulator_now_us,
    .time_diff_us = simulator_time_diff_us,
    .button_pressed = simulator_button_pressed,
    .button_edge = simulator_button_edge,
    .button_hold_ratio = simulator_button_hold_ratio,
    .buttons_reset = simulator_buttons_reset,
    .led_set = simulator_led_set,
    .synth = simulator_synth,
    .power_state = simulator_power_state,
    .sleep = simulator_noop,
    .system_reset = simulator_reset,
    .persist = simulator_noop,
    .keep_awake = simulator_noop,
    .anim_to = simulator_anim_to,
    .anim_cancel = simulator_anim_cancel,
    .leaderboard_qrcode = simulator_leaderboard,
    .button_keydown = simulator_button_keydown,
    .button_keyup = simulator_button_keyup,
    .button_keydown_tick = simulator_button_keydown_tick,
    .button_keyup_tick = simulator_button_keyup_tick,
};

static prism_button_t button_for_key(SDL_Keycode key)
{
  switch (key)
  {
  case SDLK_LEFT:
  case SDLK_a:
    return PRISM_BUTTON_LEFT;
  case SDLK_RIGHT:
  case SDLK_d:
    return PRISM_BUTTON_RIGHT;
  case SDLK_ESCAPE:
  case SDLK_SPACE:
    return PRISM_BUTTON_MENU;
  default:
    return PRISM_BUTTON_NONE;
  }
}

static void process_events(void)
{
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    if (event.type == SDL_QUIT)
    {
      running = false;
      continue;
    }
    if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP)
      continue;

    prism_button_t id = button_for_key(event.key.keysym.sym);
    simulator_button_t *button = simulator_button(id);
    if (button == NULL || event.key.repeat)
      continue;

    if (event.type == SDL_KEYUP)
    {
      button->ignored = false;
      if (button->pressed)
      {
        button->pressed = false;
        button->keyup = true;
        button->keyup_at = tick_count;
      }
    }
    else if (!button->ignored && !button->pressed)
    {
      button->pressed = true;
      button->keydown = true;
      button->keydown_at = tick_count;
      button->pressed_at = tick_count;
    }
  }
}

static void clear_button_edges(void)
{
  for (prism_button_t button = PRISM_BUTTON_LEFT;
       button <= PRISM_BUTTON_MENU; ++button)
  {
    buttons[button].keydown = false;
    buttons[button].keyup = false;
  }
}

static bool initialize_video(void)
{
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    return false;
  window = SDL_CreateWindow(
      cartridge_hello.name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      DISPLAY_WIDTH * DISPLAY_SCALE, DISPLAY_HEIGHT * DISPLAY_SCALE,
      SDL_WINDOW_SHOWN);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (renderer == NULL)
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, DISPLAY_WIDTH,
                              DISPLAY_HEIGHT);
  return window != NULL && renderer != NULL && texture != NULL;
}

static void render_frame(prism_t *prism)
{
  u8g2_ClearBuffer(&display);
  if (cartridge_hello.frame != NULL)
    cartridge_hello.frame(prism);

  const uint8_t *buffer = u8g2_GetBufferPtr(&display);
  for (uint32_t y = 0; y < DISPLAY_HEIGHT; ++y)
    for (uint32_t x = 0; x < DISPLAY_WIDTH; ++x)
    {
      uint8_t byte = buffer[x + (y / 8u) * DISPLAY_WIDTH];
      pixels[x + y * DISPLAY_WIDTH] =
          (byte & (1u << (y & 7u))) ? 0xffffffffu : 0xff000000u;
    }

  SDL_UpdateTexture(texture, NULL, pixels, DISPLAY_WIDTH * sizeof(*pixels));
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;
  if (!initialize_video())
    return EXIT_FAILURE;

  u8g2_SetupDisplay(&display, simulator_display_callback, u8x8_dummy_cb,
                    u8x8_dummy_cb, u8x8_dummy_cb);
  u8g2_SetupBuffer(&display, display_buffer, DISPLAY_HEIGHT / 8,
                   u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);

  prism_t prism = {
      .api = &simulator_api,
      .cartridge = &cartridge_hello,
  };
  void *state = calloc(1, cartridge_hello.state_size);
  if (cartridge_hello.state_size != 0 && state == NULL)
    return EXIT_FAILURE;
  prism.state = state;
  prism.state_size = cartridge_hello.state_size;

  if (cartridge_hello.enter != NULL)
    cartridge_hello.enter(&prism);

  uint64_t next_tick_us = simulator_now_us();
  uint32_t tick_remainder = 0;
  uint32_t app_phase = 0;
  uint32_t next_frame = SDL_GetTicks();
  while (running)
  {
    process_events();
    uint64_t now_us = simulator_now_us();
    while (now_us >= next_tick_us)
    {
      tick_count++;
      app_phase++;
      uint32_t divider = cartridge_hello.tick_divider == 0
                             ? 1u
                             : cartridge_hello.tick_divider;
      if (app_phase >= divider)
      {
        app_phase = 0;
        if (cartridge_hello.tick != NULL)
          cartridge_hello.tick(&prism);
        clear_button_edges();
      }
      uint32_t interval_us = 1000000u / PRISM_ENGINE_TICK_RATE;
      tick_remainder += 1000000u % PRISM_ENGINE_TICK_RATE;
      if (tick_remainder >= PRISM_ENGINE_TICK_RATE)
      {
        tick_remainder -= PRISM_ENGINE_TICK_RATE;
        interval_us++;
      }
      next_tick_us += interval_us;
      now_us = simulator_now_us();
    }
    uint32_t now = SDL_GetTicks();
    if ((int32_t)(now - next_frame) >= 0)
    {
      render_frame(&prism);
      next_frame = now + 16;
    }
    SDL_Delay(1);
  }

  if (cartridge_hello.leave != NULL)
    cartridge_hello.leave(&prism);
  free(state);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return EXIT_SUCCESS;
}
