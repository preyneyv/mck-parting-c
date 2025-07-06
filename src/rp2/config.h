#pragma once

#include <stdint.h>

#include <shared/config.h>

//// SYSTEM CONFIGURATION ////
#define SYS_CLOCK_KHZ 132000

//// DISPLAY CONFIGURATION ////
#define DISP_SPI_PORT spi0
#define DISP_PIN_CS 1
#define DISP_PIN_SCK 2
#define DISP_PIN_MOSI 3
#define DISP_PIN_RST 28
#define DISP_PIN_DC 29
#define DISP_SPI_SPEED 10 * 1000 * 1000

//// AUDIO CONFIGURATION ////
// TODO i2s
