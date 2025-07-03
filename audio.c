#include "audio.h"

#include <math.h>
#include <stdio.h>

#include <pico/stdlib.h>
#include <hardware/clocks.h>
#include <hardware/irq.h>
#include <hardware/pwm.h>
#include <hardware/sync.h>

#define BUFFER_SIZE 256
#define HALF_BUFFER_SIZE (BUFFER_SIZE / 2)

uint8_t audio_buffer[BUFFER_SIZE];
volatile int buffer_position = 0;
volatile bool need_fill_first_half = false;
volatile bool need_fill_second_half = false;

int AUDIO_PIN_SLICE = -1;
int SAMPLE_REPEAT_SHIFT = 3;

static void _audio_pwm_wrap_irq_handler()
{
    int buffer_index = buffer_position >> SAMPLE_REPEAT_SHIFT;
    pwm_clear_irq(AUDIO_PIN_SLICE);
    pwm_set_gpio_level(AUDIO_DAC_PIN, audio_buffer[buffer_index]);

    buffer_position++;
    buffer_index = buffer_position >> SAMPLE_REPEAT_SHIFT;

    // Check if we need to fill buffer halves
    if (buffer_index == HALF_BUFFER_SIZE)
    {
        need_fill_second_half = true;
    }
    else if (buffer_index >= BUFFER_SIZE)
    {
        buffer_position = 0;
        need_fill_first_half = true;
    }
}

void audio_init()
{
    AUDIO_PIN_SLICE = pwm_gpio_to_slice_num(AUDIO_DAC_PIN);

    float clk_div = 8.0f;
    uint16_t wrap = 250;

    uint32_t sys_clk_hz = clock_get_hz(clk_sys);
    float f_isr = sys_clk_hz / (clk_div * wrap);
    SAMPLE_REPEAT_SHIFT = (uint8_t)round(log2(f_isr / AUDIO_DAC_SAMPLE_RATE));

    gpio_set_function(AUDIO_DAC_PIN, GPIO_FUNC_PWM);

    pwm_clear_irq(AUDIO_PIN_SLICE);
    pwm_set_irq_enabled(AUDIO_PIN_SLICE, true);

    irq_set_exclusive_handler(PWM_IRQ_WRAP, _audio_pwm_wrap_irq_handler);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, clk_div);
    pwm_config_set_wrap(&config, wrap);
    pwm_init(AUDIO_PIN_SLICE, &config, true);

    pwm_set_gpio_level(AUDIO_DAC_PIN, 0);
}

// Synthesis parameters
const float note_hz = 440.0f;
const float vibr_hz = 0.0f;
const float vibr_depth = 0.0f;

float note_phase = 0.0f;
float vibr_phase = 0.0f;
float d_vibr_phase = vibr_hz / AUDIO_DAC_SAMPLE_RATE;

inline float add_cents(float freq, float cents)
{
    return freq * powf(2.0f, cents / 1200.0f);
}

inline float incr_wrap(float value, float inc)
{
    value += inc;
    if (value >= 1.0f)
        value -= 1.0f;
    return value;
}

static inline float _get_sample()
{
    float inst_freq = note_hz;
    if (vibr_hz > 0.0f)
    {
        float vibr_lfo = sinf(vibr_phase * 2.0f * M_PI);
        // Calculate the frequency modulation using vibrato
        inst_freq = add_cents(note_hz, vibr_lfo * vibr_depth);
    }

    note_phase = incr_wrap(note_phase, inst_freq / AUDIO_DAC_SAMPLE_RATE);
    vibr_phase = incr_wrap(vibr_phase, d_vibr_phase);

    return sinf(note_phase * 2.0f * M_PI);
}

// Synthesis function - generates audio samples
static void _synth_fill_buffer_half(uint8_t *buffer_start)
{
    for (int i = 0; i < HALF_BUFFER_SIZE; i++)
    {
        buffer_start[i] = (uint8_t)((_get_sample() + 1.0f) * 125.0f);
    }
}

void synth_fill_buffers()
{
    // Check if we need to fill buffer halves
    if (need_fill_first_half)
    {
        _synth_fill_buffer_half(&audio_buffer[0]);
        need_fill_first_half = false;
    }

    if (need_fill_second_half)
    {
        _synth_fill_buffer_half(&audio_buffer[HALF_BUFFER_SIZE]);
        need_fill_second_half = false;
    }
}
