#pragma once

#include <stdint.h>

#include <shared/config.h>

//// SYSTEM CONFIGURATION ////
#define SYS_CLOCK_HZ 132000000

//// BUTTON CONFIGURATION ////
#define BUTTON_PIN_L 10
#define BUTTON_PIN_R 11
#define BUTTON_PIN_M 12

//// DISPLAY CONFIGURATION ////
#define DISP_SPI_PORT spi0
#define DISP_SPI_SPEED (100 * 1000 * 1000)
#define DISP_RST 0
#define DISP_CS 1
#define DISP_SCK 2
#define DISP_MOSI 3
#define DISP_DC 4
#define DISP_REG_EN 5

//// AUDIO CONFIGURATION ////
#define AUDIO_I2S_LRCK 18
#define AUDIO_I2S_BCLK 19
#define AUDIO_I2S_DOUT 20
#define AUDIO_I2S_EN 21
#define AUDIO_I2S_PIO pio0

#define AUDIO_BUFFER_SIZE 128
#define AUDIO_BUFFER_POOL_SIZE 2

// todo: compute some form of audio budget and warn if exceeded

//// PERIPHERAL CONFIGURATION ////
#define PERIPH_PWR_EN 22
#define PERIPH_BAT_CHG_EN_N 23
#define PERIPH_BAT_CHG_N 25
#define PERIPH_VSYS_PGOOD_N 24
#define PERIPH_VSYS 26
#define PERIPH_VSYS_ADC 0

//// LED CONFIGURATION ////
// https://github.com/FastLED/FastLED/blob/d700beadf48f334dc299dd7b874ef459d590cad7/src/color.h
#define LED_BITRATE_HZ 800000
#define LED_CORRECTION 0xff70e0 // correction for green cast in RGB LEDs
#define LED_DOUT 13
#define LED_PIO pio0

//// VALIDATIONS ////
static_assert(AUDIO_I2S_BCLK == AUDIO_I2S_LRCK + 1,
              "AUDIO_I2S_BCLK must be LRCK + 1");
