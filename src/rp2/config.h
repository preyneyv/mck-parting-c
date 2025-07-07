#pragma once

#include <stdint.h>

#include <shared/config.h>

//// SYSTEM CONFIGURATION ////
#define SYS_CLOCK_KHZ 132000

//// INPUT CONFIGURATION ////
#define BUTTON_PIN_L 6
#define BUTTON_PIN_R 29

// todo: debouncing?

//// DISPLAY CONFIGURATION ////
#define DISP_SPI_PORT spi0
#define DISP_SPI_SPEED 100 * 1000 * 1000
#define DISP_PIN_CS 1
#define DISP_PIN_SCK 2
#define DISP_PIN_MOSI 3
#define DISP_PIN_RST 0
#define DISP_PIN_DC 7

//// AUDIO CONFIGURATION ////
#define AUDIO_I2S_DOUT 26
#define AUDIO_I2S_BCLK 27
#define AUDIO_I2S_LRCK 28

#define AUDIO_BUFFER_SIZE 512
#define AUDIO_BUFFER_POOL_SIZE 2

static const uint32_t AUDIO_BUFFER_BUDGET_MS =
    1000 * AUDIO_BUFFER_SIZE / AUDIO_SAMPLE_RATE;
