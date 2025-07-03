#include <math.h>
#include <stdio.h>

#include <pico/stdlib.h>
#include <pico/multicore.h>

#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>

#include "config.h"
#include "audio.h"

int main()
{
    stdio_init_all();
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);

    audio_init();

    while (1)
    {
        synth_fill_buffers();
        // sleep_us(100);
        // // Sleep until next buffer fill is needed
        // __wfi();
    }
}
