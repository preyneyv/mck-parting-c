#include "buffer.h"

void audio_buffer_pool_init(audio_buffer_pool_t *pool, uint8_t size,
                            uint32_t buffer_size) {
  pool->buffers = malloc(size * buffer_size * sizeof(uint32_t));
  pool->size = size;
  pool->buffer_size = buffer_size;
  pool->count = 0;
  pool->write_head = 0;
  pool->read_head = 0;
}

void audio_buffer_pool_free(audio_buffer_pool_t *pool) {
  free(pool->buffers);

  pool->buffers = NULL;

  pool->size = 0;
  pool->buffer_size = 0;

  pool->count = 0;
  pool->write_head = 0;
  pool->read_head = 0;
}

uint32_t *audio_buffer_pool_acquire_write(audio_buffer_pool_t *pool,
                                          bool blocking) {
  while (true) {
    // did we flow into unread buffers?
    __dmb();
    if (pool->count == pool->size) {
      if (!blocking)
        return NULL;
      // wait for read head to catch up
      sleep_us(100);
      continue;
    }

    uint32_t *buf = pool->buffers + (pool->write_head * pool->buffer_size);
    return buf;
  }
}

void audio_buffer_pool_commit_write(audio_buffer_pool_t *pool) {
  __dmb();
  pool->write_head = (pool->write_head + 1) % pool->size;
  pool->count++;
}

uint32_t *audio_buffer_pool_acquire_read(audio_buffer_pool_t *pool,
                                         bool blocking) {
  while (true) {
    // did we flow into written buffers?
    __dmb();
    if (pool->count == 0) {
      if (!blocking)
        return NULL;
      // wait for write head to catch up
      sleep_us(100);
      continue;
    }

    uint32_t *buf = pool->buffers + (pool->read_head * pool->buffer_size);
    return buf;
  }
}

void audio_buffer_pool_commit_read(audio_buffer_pool_t *pool) {
  __dmb();
  pool->read_head = (pool->read_head + 1) % pool->size;
  pool->count--;
}

// #include <math.h>
// #include <stdio.h>

// #include <pico/stdlib.h>
// #include <hardware/clocks.h>
// #include <hardware/irq.h>
// #include <hardware/pwm.h>
// #include <hardware/sync.h>

// #define BUFFER_SIZE 256
// #define HALF_BUFFER_SIZE (BUFFER_SIZE / 2)

// uint8_t audio_buffer[BUFFER_SIZE];
// volatile int buffer_position = 0;
// volatile bool need_fill_first_half = false;
// volatile bool need_fill_second_half = false;

// int AUDIO_PIN_SLICE = -1;
// int SAMPLE_REPEAT_SHIFT = 3;

// // Sine lookup table
// #define SIN_LUT_SIZE 1024
// static float sin_lut[SIN_LUT_SIZE];

// // Exponential lookup table for 2^x over x in [-0.5, 0.5]
// #define EXP_LUT_SIZE 128
// static float exp_lut[EXP_LUT_SIZE];
// #define INV_1200F (1.0f / 1200.0f)

// // Fast sine lookup (phase in [0,1))
// static inline float f_sin(float phase)
// {
//     int idx = (int)(phase * SIN_LUT_SIZE) & (SIN_LUT_SIZE - 1);
//     return sin_lut[idx];
// }

// static inline float f_exp(float x)
// {
//     // clamp to LUT range
//     if (x < -0.5f)
//         x = -0.5f;
//     else if (x > 0.5f)
//         x = 0.5f;
//     // map to index
//     int idx = (int)((x + 0.5f) * (EXP_LUT_SIZE - 1) + 0.5f);
//     return exp_lut[idx];
// }

// static void _audio_pwm_wrap_irq_handler()
// {
//     int buffer_index = buffer_position >> SAMPLE_REPEAT_SHIFT;
//     pwm_clear_irq(AUDIO_PIN_SLICE);
//     pwm_set_gpio_level(AUDIO_DAC_PIN, audio_buffer[buffer_index]);

//     buffer_position++;
//     buffer_index = buffer_position >> SAMPLE_REPEAT_SHIFT;

//     // Check if we need to fill buffer halves
//     if (buffer_index == HALF_BUFFER_SIZE)
//     {
//         need_fill_second_half = true;
//     }
//     else if (buffer_index >= BUFFER_SIZE)
//     {
//         buffer_position = 0;
//         need_fill_first_half = true;
//     }
// }

// void audio_init()
// {
//     // Initialize sine lookup table
//     for (int i = 0; i < SIN_LUT_SIZE; i++)
//     {
//         sin_lut[i] = sinf((float)i / SIN_LUT_SIZE * 2.0f * M_PI);
//     }

//     // Initialize exponential lookup table
//     for (int i = 0; i < EXP_LUT_SIZE; i++)
//     {
//         float x = (float)i / (EXP_LUT_SIZE - 1) - 0.5f; // [-0.5 .. +0.5]
//         exp_lut[i] = exp2f(x);
//     }

//     AUDIO_PIN_SLICE = pwm_gpio_to_slice_num(AUDIO_DAC_PIN);

//     float clk_div = 8.0f;
//     uint16_t wrap = 250;

//     uint32_t sys_clk_hz = clock_get_hz(clk_sys);
//     float f_isr = sys_clk_hz / (clk_div * wrap);
//     SAMPLE_REPEAT_SHIFT = (uint8_t)round(log2(f_isr /
//     AUDIO_SAMPLE_RATE));

//     gpio_set_function(AUDIO_DAC_PIN, GPIO_FUNC_PWM);

//     pwm_clear_irq(AUDIO_PIN_SLICE);
//     pwm_set_irq_enabled(AUDIO_PIN_SLICE, true);

//     irq_set_exclusive_handler(PWM_IRQ_WRAP, _audio_pwm_wrap_irq_handler);
//     irq_set_enabled(PWM_IRQ_WRAP, true);

//     pwm_config config = pwm_get_default_config();
//     pwm_config_set_clkdiv(&config, clk_div);
//     pwm_config_set_wrap(&config, wrap);
//     pwm_init(AUDIO_PIN_SLICE, &config, true);

//     pwm_set_gpio_level(AUDIO_DAC_PIN, 0);
// }

// // Synthesis parameters
// const float note_hz = 440.0f;
// const float vibr_hz = 0.2f;
// const float vibr_depth = 500.0f;

// float note_phase = 0.0f;
// float vibr_phase = 0.0f;
// float d_vibr_phase = vibr_hz / AUDIO_SAMPLE_RATE;

// static inline float add_cents(float freq, float cents)
// {
//     float x = cents * INV_1200F;
//     return freq * f_exp(x);
// }

// inline float incr_wrap(float value, float inc)
// {
//     value += inc;
//     if (value >= 1.0f)
//         value -= 1.0f;
//     return value;
// }

// static inline float _get_sample()
// {
//     float inst_freq = note_hz;
//     if (vibr_hz > 0.0f)
//     {
//         // Use lookup table for vibrato LFO
//         float vibr_lfo = f_sin(vibr_phase);
//         inst_freq = add_cents(note_hz, vibr_lfo * vibr_depth);
//     }

//     note_phase = incr_wrap(note_phase, inst_freq / AUDIO_SAMPLE_RATE);
//     vibr_phase = incr_wrap(vibr_phase, d_vibr_phase);

//     // Return sample from lookup table
//     return f_sin(note_phase);
// }

// // Synthesis function - generates audio samples
// static void _synth_fill_buffer_half(uint8_t *buffer_start)
// {
//     for (int i = 0; i < HALF_BUFFER_SIZE; i++)
//     {
//         buffer_start[i] = (uint8_t)((_get_sample() + 1.0f) * 125.0f);
//     }
// }

// void synth_fill_buffers()
// {
//     // Check if we need to fill buffer halves
//     if (need_fill_first_half)
//     {
//         _synth_fill_buffer_half(&audio_buffer[0]);
//         need_fill_first_half = false;
//     }

//     if (need_fill_second_half)
//     {
//         _synth_fill_buffer_half(&audio_buffer[HALF_BUFFER_SIZE]);
//         need_fill_second_half = false;
//     }
// }
