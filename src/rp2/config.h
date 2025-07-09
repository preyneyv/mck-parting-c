#pragma once

#include <stdint.h>

#include <shared/config.h>

//// SYSTEM CONFIGURATION ////
#define SYS_CLOCK_HZ 132000000

//// INPUT CONFIGURATION ////
#define BUTTON_PIN_L 6
#define BUTTON_PIN_R 29

// todo: debouncing?

//// DISPLAY CONFIGURATION ////
#define DISP_SPI_PORT spi0
#define DISP_SPI_SPEED 100 * 1000 * 1000
#define DISP_PIN_RST 0
#define DISP_PIN_CS 1
#define DISP_PIN_SCK 2
#define DISP_PIN_MOSI 3
#define DISP_PIN_DC 4

//// AUDIO CONFIGURATION ////
#define AUDIO_I2S_DOUT 26
#define AUDIO_I2S_BCLK 27
#define AUDIO_I2S_LRCK 28
#define AUDIO_I2S_PIO pio0

#define AUDIO_BUFFER_SIZE 128
#define AUDIO_BUFFER_POOL_SIZE 2

// todo: compute some form of audio budget and warn if exceeded
static const uint32_t AUDIO_BUFFER_BUDGET_MS =
    1000 * AUDIO_BUFFER_SIZE / AUDIO_SAMPLE_RATE;

//// VALIDATIONS ////
static_assert(AUDIO_I2S_BCLK == AUDIO_I2S_DOUT + 1,
              "AUDIO_I2S_BCLK must be DOUT + 1");
static_assert(AUDIO_I2S_LRCK == AUDIO_I2S_BCLK + 1,
              "AUDIO_I2S_LRCK must be BCLK + 1");
