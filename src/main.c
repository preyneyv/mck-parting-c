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

void core1_entry()
{
    audio_init();
    while (1)
    {
        synth_fill_buffers();
        __wfi();
    }
}

int main()
{
    stdio_init_all();
    set_sys_clock_khz(SYS_CLOCK_KHZ, true);

    multicore_reset_core1();
    multicore_launch_core1(core1_entry);

    while (1)
    {
        __wfi();
    }
}
