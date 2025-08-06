#include <hardware/spi.h>
#include <pico/stdlib.h>

#include <u8g2.h>

#include <shared/display.h>

#include "config.h"

static uint8_t _byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,
                        void *arg_ptr) {
  uint8_t *data;
  switch (msg) {
  case U8X8_MSG_BYTE_SEND:
    data = (uint8_t *)arg_ptr;
    spi_write_blocking(DISP_SPI_PORT, data, arg_int);
    break;
  case U8X8_MSG_BYTE_INIT:
    u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
    break;
  case U8X8_MSG_BYTE_SET_DC:
    u8x8_gpio_SetDC(u8x8, arg_int);
    break;
  case U8X8_MSG_BYTE_START_TRANSFER:
    u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
    u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO,
                            u8x8->display_info->post_chip_enable_wait_ns, NULL);
    break;
  case U8X8_MSG_BYTE_END_TRANSFER:
    u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO,
                            u8x8->display_info->pre_chip_disable_wait_ns, NULL);
    u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
    break;
  default:
    return 0;
  }
  return 1;
}

static uint8_t _gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int,
                                  void *arg_ptr) {
  switch (msg) {
  case U8X8_MSG_GPIO_AND_DELAY_INIT:
    spi_init(DISP_SPI_PORT, DISP_SPI_SPEED);
    gpio_set_function(DISP_CS, GPIO_FUNC_SIO);
    gpio_set_function(DISP_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DISP_MOSI, GPIO_FUNC_SPI);
    gpio_init(DISP_RST);
    gpio_init(DISP_DC);
    gpio_init(DISP_CS);
    gpio_set_dir(DISP_RST, GPIO_OUT);
    gpio_set_dir(DISP_DC, GPIO_OUT);
    gpio_set_dir(DISP_CS, GPIO_OUT);
    gpio_put(DISP_RST, 1);
    gpio_put(DISP_CS, 1);
    gpio_put(DISP_DC, 0);
    break;
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
  case U8X8_MSG_GPIO_CS: // CS (chip select) pin: Output level in arg_int
    gpio_put(DISP_CS, arg_int);
    break;
  case U8X8_MSG_GPIO_DC: // DC (data/cmd, A0, register select) pin: Output level
    gpio_put(DISP_DC, arg_int);
    break;
  case U8X8_MSG_GPIO_RESET: // Reset pin: Output level in arg_int
    gpio_put(DISP_RST,
             arg_int); // printf("U8X8_MSG_GPIO_RESET %d\n", arg_int);
    break;
  default:
    u8x8_SetGPIOResult(u8x8, 1); // default return value
    break;
  }
  return 1;
}

void display_init(display_t *display) {
  display->enabled = false;

  u8g2_t *u8g2 = display_get_u8g2(display);
  u8g2_Setup_sh1107_64x128_f(u8g2, U8G2_R1, _byte_cb, _gpio_and_delay_cb);
  u8g2_InitDisplay(u8g2);

  gpio_init(DISP_REG_EN);
  gpio_set_dir(DISP_REG_EN, GPIO_OUT);
  gpio_put(DISP_REG_EN, 0); // keep display off until needed.
}

static void display_on(display_t *display) {
  // enable regulator
  gpio_put(DISP_REG_EN, 1);
  sleep_ms(100); // wait for regulator to stabilize

  u8g2_t *u8g2 = display_get_u8g2(display);
  u8g2_SetPowerSave(u8g2, 0);
  u8g2_SetContrast(u8g2, 255);
}

static void display_off(display_t *display) {
  u8g2_t *u8g2 = display_get_u8g2(display);
  u8g2_SetPowerSave(u8g2, 1);

  sleep_ms(100); // wait for disp off flush
  // disable regulator
  gpio_put(DISP_REG_EN, 0);
}

void display_set_enabled(display_t *display, bool enabled) {
  if (display->enabled == enabled) {
    return;
  }
  display->enabled = enabled;

  if (enabled) {
    display_on(display);
  } else {
    display_off(display);
  }
}
