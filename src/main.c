/**
 * Core0: Main UI / gameplay loop
 * Core1: Audio synthesis and output
 */

#include <math.h>
#include <stdio.h>

#include <pico/stdlib.h>
#include <pico/multicore.h>

#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>
#include <hardware/spi.h>

#include <u8g2.h>

#include "utils/timing.h"
#include "config.h"
#include "audio.h"

u8g2_t u8g2;

#define SPI_PORT spi0
#define PIN_CS 1
#define PIN_SCK 2
#define PIN_MOSI 3
#define SPI_SPEED 100 * 1000 * 1000
#define PIN_DC 29
#define PIN_RST 28

uint8_t u8x8_byte_pico_hw_spi(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    uint8_t *data;
    switch (msg)
    {
    case U8X8_MSG_BYTE_SEND:
        data = (uint8_t *)arg_ptr;
        spi_write_blocking(SPI_PORT, data, arg_int);
        break;
    case U8X8_MSG_BYTE_INIT:
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_disable_level);
        break;
    case U8X8_MSG_BYTE_SET_DC:
        u8x8_gpio_SetDC(u8x8, arg_int);
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        u8x8_gpio_SetCS(u8x8, u8x8->display_info->chip_enable_level);
        u8x8->gpio_and_delay_cb(u8x8, U8X8_MSG_DELAY_NANO, u8x8->display_info->post_chip_enable_wait_ns, NULL);
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

uint8_t u8x8_gpio_and_delay_pico(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        spi_init(SPI_PORT, SPI_SPEED);
        gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
        gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
        gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
        gpio_init(PIN_RST);
        gpio_init(PIN_DC);
        gpio_init(PIN_CS);
        gpio_set_dir(PIN_RST, GPIO_OUT);
        gpio_set_dir(PIN_DC, GPIO_OUT);
        gpio_set_dir(PIN_CS, GPIO_OUT);
        gpio_put(PIN_RST, 1);
        gpio_put(PIN_CS, 1);
        gpio_put(PIN_DC, 0);
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
        gpio_put(PIN_CS, arg_int);
        break;
    case U8X8_MSG_GPIO_DC: // DC (data/cmd, A0, register select) pin: Output level
        gpio_put(PIN_DC, arg_int);
        break;
    case U8X8_MSG_GPIO_RESET:       // Reset pin: Output level in arg_int
        gpio_put(PIN_RST, arg_int); // printf("U8X8_MSG_GPIO_RESET %d\n", arg_int);
        break;
    default:
        u8x8_SetGPIOResult(u8x8, 1); // default return value
        break;
    }
    return 1;
}

void setup_display()
{
    // Use software SPI implementation for testing
    u8g2_Setup_ssd1306_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8x8_byte_pico_hw_spi,
        u8x8_gpio_and_delay_pico);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);
}

void core1_main()
{
    audio_init();
    while (1)
    {
        synth_fill_buffers();
        __wfi();
    }
}

const uint8_t WIDTH = 128;
const uint8_t HEIGHT = 64;
const uint32_t PIX = WIDTH * HEIGHT;

int main()
{
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);
    stdio_init_all();

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    setup_display();

    TimingInstrumenter ti_tick;
    TimingInstrumenter ti_show;

    uint64_t last_frame_us = 0;
    uint64_t last_log_us = 0;
    uint32_t last_log_frames = 0;
    uint32_t fps = 0;

    uint32_t i = 0;
    while (1)
    {
        ti_start(&ti_tick);
        i = (i + 1) % PIX;
        uint8_t x = i % WIDTH;
        uint8_t y = i / WIDTH;

        u8g2_FirstPage(&u8g2);
        u8g2_ClearBuffer(&u8g2);
        u8g2_SetDrawColor(&u8g2, 0);
        u8g2_DrawBox(&u8g2, 0, 0, 128, 64);

        u8g2_SetDrawColor(&u8g2, 1);
        u8g2_DrawPixel(&u8g2, x, y); // Draw a pixel that moves diagonally
        ti_stop(&ti_tick);

        ti_start(&ti_show);
        u8g2_SendBuffer(&u8g2);
        ti_stop(&ti_show);

        last_log_frames++;
        uint64_t now = time_us_64();
        if (now - last_log_us > 1000000) // Log every second
        {
            fps = last_log_frames;
            float ti_tick_avg = ti_get_average_ms(&ti_tick, true);
            float ti_show_avg = ti_get_average_ms(&ti_show, true);
            float ti_frame_avg = ti_tick_avg + ti_show_avg;
            printf("fps: %d | frame: %.2f ms | tick: %.2f ms | show: %.2f ms\n",
                   fps,
                   ti_frame_avg,
                   ti_tick_avg,
                   ti_show_avg);
            last_log_us = now;
            last_log_frames = 0;
        }

        uint64_t spent_us = now - last_frame_us;
        if (spent_us < TARGET_FRAME_INTERVAL_US)
        {
            sleep_us(TARGET_FRAME_INTERVAL_US - spent_us);
            last_frame_us = time_us_64();
        }
        else
        {
            last_frame_us = now;
        }
    }
}
