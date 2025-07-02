#include <math.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"
#include "hardware/sync.h"

#define AUDIO_PIN 26

#include "sample.h"

// Double buffering system
#define BUFFER_SIZE 256
#define HALF_BUFFER_SIZE (BUFFER_SIZE / 2)

uint8_t audio_buffer[BUFFER_SIZE];
volatile int buffer_position = 0;
volatile bool need_fill_first_half = false;
volatile bool need_fill_second_half = false;

int audio_pin_slice = -1;

// Synthesis parameters
float phase = 0.0f;
float frequency = 440.0f;     // A4 note
float sample_rate = 88000.0f; // Effective sample rate

void pwm_wrap_irq_handler()
{
    pwm_clear_irq(audio_pin_slice);

    // Play current sample from buffer
    pwm_set_gpio_level(AUDIO_PIN, audio_buffer[buffer_position]);

    buffer_position++;

    // Check if we need to fill buffer halves
    if (buffer_position == HALF_BUFFER_SIZE)
    {
        need_fill_second_half = true;
    }
    else if (buffer_position >= BUFFER_SIZE)
    {
        buffer_position = 0;
        need_fill_first_half = true;
    }
}

// Synthesis function - generates audio samples
void synthesize_buffer_half(uint8_t *buffer_start)
{
    for (int i = 0; i < HALF_BUFFER_SIZE; i++)
    {
        // Simple sine wave synthesis
        float sample = sinf(phase * 2.0f * M_PI);

        // Convert to 8-bit PWM value (0-250 range to match PWM wrap)
        buffer_start[i] = (uint8_t)((sample + 1.0f) * 125.0f);

        // Update phase for next sample
        phase += frequency / sample_rate;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
        }
    }
}

int main()
{
    stdio_init_all();
    audio_pin_slice = pwm_gpio_to_slice_num(AUDIO_PIN);

    set_sys_clock_khz(176000, true);
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);

    // Initialize both buffer halves with synthesized audio
    synthesize_buffer_half(&audio_buffer[0]);
    synthesize_buffer_half(&audio_buffer[HALF_BUFFER_SIZE]);

    pwm_clear_irq(audio_pin_slice);
    pwm_set_irq_enabled(audio_pin_slice, true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_wrap_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 8.0f);
    pwm_config_set_wrap(&config, 250);
    pwm_init(audio_pin_slice, &config, true);

    pwm_set_gpio_level(AUDIO_PIN, 0);

    while (1)
    {
        // Check if we need to fill buffer halves
        if (need_fill_first_half)
        {
            synthesize_buffer_half(&audio_buffer[0]);
            need_fill_first_half = false;
        }

        if (need_fill_second_half)
        {
            synthesize_buffer_half(&audio_buffer[HALF_BUFFER_SIZE]);
            need_fill_second_half = false;
        }

        // Sleep until next buffer fill is needed
        __wfi();
    }
}
