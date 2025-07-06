#include <assert.h>

#include <pico/stdlib.h>

#include <SDL.h>
#include <u8g2.h>

#include "config.h"
#include "display.h"

const uint8_t RES_MULT = 3;

static uint16_t pixel_width;
static uint16_t pixel_height;
static uint16_t render_width;
static uint16_t render_height;
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;

static const u8x8_display_info_t u8x8_sdl_128x64_info = {
    /* chip_enable_level = */ 0,
    /* chip_disable_level = */ 1,

    /* post_chip_enable_wait_ns = */ 0,
    /* pre_chip_disable_wait_ns = */ 0,
    /* reset_pulse_width_ms = */ 0,
    /* post_reset_wait_ms = */ 0,
    /* sda_setup_time_ns = */ 0,
    /* sck_pulse_width_ns = */ 0,
    /* sck_clock_hz = */ 0UL,
    /* spi_mode = */ 1,
    /* i2c_bus_clock_100kHz = */ 0,
    /* data_setup_time_ns = */ 0,
    /* write_pulse_width_ns = */ 0,
    /* tile_width = */ 16,
    /* tile_hight = */ 8,
    /* default_x_offset = */ 0,
    /* flipmode_x_offset = */ 0,
    /* pixel_width = */ 128,
    /* pixel_height = */ 64};

static inline void _display_cb_display_init(u8x8_t *u8x8) {
  pixel_width = u8x8->display_info->pixel_width;
  pixel_height = u8x8->display_info->pixel_height;

  render_width = pixel_width * RES_MULT;
  render_height = pixel_height * RES_MULT;

  SDL_Init(SDL_INIT_VIDEO);
  window = SDL_CreateWindow(PROJECT_NAME, SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED, render_width,
                            render_height, SDL_WINDOW_SHOWN);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB888,
                        SDL_TEXTUREACCESS_STREAMING, pixel_width, pixel_height);
}

static inline void _display_cb_draw_tile(u8x8_t *u8x8, uint8_t arg_int,
                                         u8x8_tile_t *params,
                                         SDL_Texture *tex) {
  uint8_t x0 = params->x_pos * 8 + u8x8->x_offset;
  uint8_t y0 = params->y_pos * 8;
  uint8_t cnt = params->cnt;
  uint8_t *tile = params->tile_ptr;

  // The total width to update is cnt * 8 * arg_int
  SDL_Rect rect = {.x = x0, .y = y0, .w = cnt * 8 * arg_int, .h = 8};
  void *pixels;
  int pitch;
  SDL_LockTexture(tex, &rect, &pixels, &pitch);
  uint32_t *dst = (uint32_t *)pixels;
  int row_stride = pitch / sizeof(uint32_t);

  // For each repetition
  for (uint8_t rep = 0; rep < arg_int; ++rep) {
    // For each tile in the sequence
    for (uint8_t t = 0; t < cnt; ++t) {
      // For each column in the tile (8 columns)
      for (uint8_t col = 0; col < 8; ++col) {
        uint8_t col_byte = tile[t * 8 + col];
        // For each row in the tile (8 rows)
        for (uint8_t row = 0; row < 8; ++row) {
          uint32_t color = (col_byte >> row) & 1 ? 0xFFFFFFFF : 0xFF000000;
          // Compute destination x within the locked rect
          int dx = (rep * cnt * 8) + (t * 8) + col;
          dst[row * row_stride + dx] = color;
        }
      }
    }
  }
  SDL_UnlockTexture(tex);
}

static inline void _display_cb_refresh() {
  SDL_RenderClear(renderer);
  SDL_RenderCopy(
      renderer, texture, NULL,
      &(SDL_Rect){.x = 0, .y = 0, .w = render_width, .h = render_height});
  SDL_RenderPresent(renderer);
}

static uint8_t _display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,
                           void *arg_ptr) {

  switch (msg) {
  case U8X8_MSG_DISPLAY_SETUP_MEMORY:
    u8x8_d_helper_display_setup_memory(u8x8, &u8x8_sdl_128x64_info);
    break;
  case U8X8_MSG_DISPLAY_INIT:
    u8x8_d_helper_display_init(u8x8);
    _display_cb_display_init(u8x8);
    break;
  case U8X8_MSG_DISPLAY_DRAW_TILE:
    _display_cb_draw_tile(u8x8, arg_int, (u8x8_tile_t *)arg_ptr, texture);
    break;
  case U8X8_MSG_DISPLAY_REFRESH:
    _display_cb_refresh();
    break;
  case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
  case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
  case U8X8_MSG_DISPLAY_SET_CONTRAST:
    // TODO: maybe support these?
    break;
  default:
    return 0; // unsupported message
  }
  return 1;
}

static uint8_t _gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,
                                  void *arg_ptr) {
  switch (msg) {
  case U8X8_MSG_DELAY_NANO: // delay arg_int * 1 nano second
    sleep_us((999 + arg_int) / 1000);
    break;
  case U8X8_MSG_DELAY_100NANO: // delay arg_int * 100 nano seconds
    sleep_us((9 + arg_int) / 10);
    break;
  case U8X8_MSG_DELAY_10MICRO: // delay arg_int * 10 micro seconds
    sleep_us(arg_int * 10);
    break;
  case U8X8_MSG_DELAY_MILLI: // delay arg_int * 1 milli second
    sleep_ms(arg_int);
    break;
  default:
    u8x8_SetGPIOResult(u8x8, 1); // default return value
    break;
  }
  return 1;
}

static void u8g2_SetupSDL_128x64_f(u8g2_t *u8g2) {
  u8g2_SetupDisplay(u8g2,
                    /* display_cb */ _display_cb,
                    /* cad_cb */ u8x8_dummy_cb,
                    /* byte_cb */ u8x8_dummy_cb,
                    /* gpio_and_delay_cb */ _gpio_and_delay_cb);
  uint8_t tile_buf_height;
  uint8_t *buf;
  buf = u8g2_m_16_8_f(&tile_buf_height);
  u8g2_SetupBuffer(u8g2, buf, tile_buf_height, u8g2_ll_hvline_vertical_top_lsb,
                   U8G2_R0);
}

void display_init(display_t *display) {
  u8g2_t *u8g2 = display_get_u8g2(display);
  u8g2_SetupSDL_128x64_f(u8g2);
  u8g2_InitDisplay(u8g2);
  u8g2_SetPowerSave(u8g2, 0);
}
