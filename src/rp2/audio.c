#include <stdio.h>

#include <hardware/dma.h>
#include <hardware/pio.h>

#include <shared/audio/buffer.h>
#include <shared/audio/synth.h>
#include <shared/utils/timing.h>

#include "audio.h"
#include "audio.pio.h"
#include "config.h"

// Math for PIO frequency calculations
static const uint32_t BCLK_HZ =
    AUDIO_SAMPLE_RATE * AUDIO_BIT_DEPTH * 2; // 2 channels
static const uint32_t LRCK_HZ = AUDIO_SAMPLE_RATE;
static const uint32_t SM_BCLK = 2 * BCLK_HZ; // 2 clocks per bit

static const uint32_t SM_CLKDIV_INT = SYS_CLOCK_HZ / SM_BCLK;
static const uint32_t SM_CLKDIV_FRAC = (SYS_CLOCK_HZ % SM_BCLK) * 256 / SM_BCLK;

// Shared state
static const uint32_t SILENT_BUFFER[AUDIO_BUFFER_SIZE] = {0};
static audio_buffer_pool_t pool;
int dma_channel;

// initialize pio state machine for i2s
static void audio_playback_write_pio_init(PIO pio, uint8_t sm) {
  int offset = pio_add_program(pio, &audio_playback_write_program);
  if (offset < 0) {
    panic("Failed to add audio playback write program to PIO");
  }

  pio_gpio_init(pio, AUDIO_I2S_DOUT);
  pio_gpio_init(pio, AUDIO_I2S_BCLK);
  pio_gpio_init(pio, AUDIO_I2S_LRCK);

  pio_sm_config c = audio_playback_write_program_get_default_config(offset);
  sm_config_set_out_pins(&c, AUDIO_I2S_DOUT, 1);
  sm_config_set_sideset_pins(&c, AUDIO_I2S_BCLK);
  sm_config_set_out_shift(&c, false, false, AUDIO_BIT_DEPTH);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  pio_sm_init(pio, sm, offset, &c);

  uint32_t pins = 0b111 << AUDIO_I2S_DOUT; // DOUT, BCLK, LRCK
  pio_sm_set_pins_with_mask(pio, sm, 0, pins);
  pio_sm_set_pindirs_with_mask(pio, sm, pins, pins);

  pio_sm_set_clkdiv_int_frac8(pio, sm, SM_CLKDIV_INT, SM_CLKDIV_FRAC);
  pio_sm_set_enabled(pio, sm, true);

  printf("pio sm %d clkdiv: %f\n", sm, SM_CLKDIV_INT + SM_CLKDIV_FRAC / 256.0);
}

static void __isr audio_playback_write_dma_irq_handler(void) {
  // clear the interrupt
  dma_hw->ints0 = 1u << dma_channel;

  static bool using_pool_buffer = false;

  if (using_pool_buffer) {
    // return borrowed buffer to pool
    audio_buffer_pool_commit_read(&pool);
  }
  // get next buffer if available, otherwise use silent buffer
  audio_buffer_t next_buffer = audio_buffer_pool_acquire_read(&pool, false);
  if (next_buffer == NULL) {
    // no buffer available, use silent buffer
    dma_channel_set_read_addr(dma_channel, SILENT_BUFFER, true);
    using_pool_buffer = false;
  } else {
    dma_channel_set_read_addr(dma_channel, next_buffer, true);
    using_pool_buffer = true;
  }
}

// initialize dma for copying into pio tx fifo
static void audio_playback_write_dma_init(PIO pio, uint8_t sm) {
  dma_channel = dma_claim_unused_channel(true);
  dma_channel_config c = dma_channel_get_default_config(dma_channel);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);

  // read from silent buffer first (will be swapped after first write)
  dma_channel_configure(dma_channel, &c, &pio->txf[sm], SILENT_BUFFER,
                        AUDIO_BUFFER_SIZE, false);

  dma_channel_set_irq0_enabled(dma_channel, true);
  irq_set_exclusive_handler(DMA_IRQ_0, audio_playback_write_dma_irq_handler);
  irq_set_enabled(DMA_IRQ_0, true);

  dma_channel_start(dma_channel);
}

// setup DMA and PIO for audio playback
static void audio_playback_begin() {
  uint8_t sm = pio_claim_unused_sm(AUDIO_I2S_PIO, true);

  audio_playback_write_pio_init(AUDIO_I2S_PIO, sm);
  audio_playback_write_dma_init(AUDIO_I2S_PIO, sm);
}

void audio_init() {
  audio_buffer_pool_init(&pool, AUDIO_BUFFER_POOL_SIZE, AUDIO_BUFFER_SIZE);

  audio_playback_begin();

  TimingInstrumenter ti_synth;

  audio_synth_t synth;
  audio_synth_init(&synth, AUDIO_SAMPLE_RATE);
  synth.master_level = q1x15_f(1.f);

  synth.voices[0].ops[0].config = (audio_synth_operator_config_t){
      .freq_mult = 12.0f,
      .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
      .level = q1x15_f(.0f),
  };
  synth.voices[0].ops[1].config = (audio_synth_operator_config_t){
      .freq_mult = 1.0f,
      .mode = AUDIO_SYNTH_OP_MODE_ADDITIVE,
      .level = q1x15_f(1.f),
  };
  audio_synth_voice_set_freq(&synth.voices[0], 200.0f); // A3

  int i = 0;
  while (true) {
    audio_buffer_t buffer = audio_buffer_pool_acquire_write(&pool, true);
    if (i < 5000) {
      synth.master_level = q1x15_f(1.f);
    } else {
      synth.master_level = Q1X15_ZERO;
    }
    ti_start(&ti_synth);
    audio_synth_fill_buffer(&synth, buffer, pool.buffer_size);
    ti_stop(&ti_synth);
    audio_buffer_pool_commit_write(&pool);

    i += 1;
    if (i % 100 == 0) {
      printf("synth: %f ms\n", ti_get_average_ms(&ti_synth, true));
    }
    if (i > 10000) {
      i -= 10000;
    }
  }
}
