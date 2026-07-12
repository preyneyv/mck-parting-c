#include <stdbool.h>

#include <SDL.h>
#include <u8g2.h>

#include <platform/display.h>
#include <platform/time.h>

#include "config.h"

enum { DISPLAY_SCALE = 3 };

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static u8g2_t display;
static uint16_t render_width;
static uint16_t render_height;
static uint8_t contrast = 255;
static bool enabled = true;

static const u8x8_display_info_t display_info = {
    .chip_enable_level = 0,
    .chip_disable_level = 1,
    .spi_mode = 1,
    .tile_width = 16,
    .tile_height = 8,
    .pixel_width = 128,
    .pixel_height = 64,
};

static void initialize_window(const u8x8_t *u8x8)
{
  render_width = u8x8->display_info->pixel_width * DISPLAY_SCALE;
  render_height = u8x8->display_info->pixel_height * DISPLAY_SCALE;
  SDL_InitSubSystem(SDL_INIT_VIDEO);
  window = SDL_CreateWindow(PROJECT_NAME, SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, render_width,
                            render_height, SDL_WINDOW_SHOWN);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (renderer == NULL)
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING,
                              u8x8->display_info->pixel_width,
                              u8x8->display_info->pixel_height);
}

static void draw_tiles(const u8x8_t *u8x8, uint8_t repetitions,
                       const u8x8_tile_t *tiles)
{
  SDL_Rect area = {
      .x = tiles->x_pos * 8 + u8x8->x_offset,
      .y = tiles->y_pos * 8,
      .w = tiles->cnt * 8 * repetitions,
      .h = 8,
  };
  void *pixels;
  int pitch;
  if (SDL_LockTexture(texture, &area, &pixels, &pitch) != 0)
    return;

  uint32_t *destination = pixels;
  int stride = pitch / (int)sizeof(*destination);
  uint32_t white = enabled
                       ? 0xff000000u | (uint32_t)contrast * 0x00010101u
                       : 0xff000000u;
  for (uint8_t repetition = 0; repetition < repetitions; ++repetition)
    for (uint8_t tile = 0; tile < tiles->cnt; ++tile)
      for (uint8_t column = 0; column < 8; ++column)
      {
        uint8_t bits = tiles->tile_ptr[tile * 8 + column];
        int x = repetition * tiles->cnt * 8 + tile * 8 + column;
        for (uint8_t row = 0; row < 8; ++row)
          destination[row * stride + x] =
              (bits & (1u << row)) != 0 ? white : 0xff000000u;
      }
  SDL_UnlockTexture(texture);
}

static void present(void)
{
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL,
                 &(SDL_Rect){.w = render_width, .h = render_height});
  SDL_RenderPresent(renderer);
}

static uint8_t display_callback(u8x8_t *u8x8, uint8_t message,
                                uint8_t argument, void *data)
{
  switch (message)
  {
  case U8X8_MSG_DISPLAY_SETUP_MEMORY:
    u8x8_d_helper_display_setup_memory(u8x8, &display_info);
    return 1;
  case U8X8_MSG_DISPLAY_INIT:
    u8x8_d_helper_display_init(u8x8);
    initialize_window(u8x8);
    return 1;
  case U8X8_MSG_DISPLAY_DRAW_TILE:
    draw_tiles(u8x8, argument, data);
    return 1;
  case U8X8_MSG_DISPLAY_REFRESH:
    present();
    return 1;
  case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
  case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
  case U8X8_MSG_DISPLAY_SET_CONTRAST:
    return 1;
  default:
    return 0;
  }
}

static uint8_t delay_callback(u8x8_t *u8x8, uint8_t message,
                              uint8_t argument, void *data)
{
  (void)data;
  switch (message)
  {
  case U8X8_MSG_DELAY_NANO:
    platform_sleep_us((999u + argument) / 1000u);
    break;
  case U8X8_MSG_DELAY_100NANO:
    platform_sleep_us((9u + argument) / 10u);
    break;
  case U8X8_MSG_DELAY_10MICRO:
    platform_sleep_us(argument * 10u);
    break;
  case U8X8_MSG_DELAY_MILLI:
    platform_sleep_ms(argument);
    break;
  default:
    u8x8_SetGPIOResult(u8x8, 1);
    break;
  }
  return 1;
}

void platform_display_init(void)
{
  u8g2_SetupDisplay(&display, display_callback, u8x8_dummy_cb, u8x8_dummy_cb,
                    delay_callback);
  uint8_t tile_height;
  uint8_t *buffer = u8g2_m_16_8_f(&tile_height);
  u8g2_SetupBuffer(&display, buffer, tile_height,
                   u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
  u8g2_InitDisplay(&display);
  u8g2_SetPowerSave(&display, 0);
}

void platform_display_set_enabled(bool value) { enabled = value; }
void platform_display_set_contrast(uint8_t value) { contrast = value; }
u8g2_t *platform_display_get_u8g2(void) { return &display; }
