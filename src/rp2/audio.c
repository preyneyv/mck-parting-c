#include <stdio.h>

#include <hardware/dma.h>
#include <hardware/pio.h>

#include <shared/audio.h>
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
static const uint32_t SM_BCLK = BCLK_HZ; // 2 clocks per bit

static const uint32_t SM_CLKDIV_INT = SYS_CLOCK_HZ / SM_BCLK;
static const uint32_t SM_CLKDIV_FRAC = (SYS_CLOCK_HZ % SM_BCLK) * 256 / SM_BCLK;

// todo: stop clocking bclk / lrck when not playing audio for power saving

// Shared state
static const uint32_t SILENT_BUFFER[AUDIO_BUFFER_SIZE] = {0};
static audio_buffer_pool_t pool;
static int dma_channel;

// initialize pio state machine for i2s
static void audio_playback_write_pio_init(PIO pio, uint8_t sm) {
  int offset = pio_add_program(pio, &audio_playback_write_program);
  if (offset < 0) {
    panic("Failed to add audio playback write program to PIO");
  }

  pio_gpio_init(pio, AUDIO_I2S_LRCK);
  pio_gpio_init(pio, AUDIO_I2S_BCLK);
  pio_gpio_init(pio, AUDIO_I2S_DOUT);
  pio_sm_set_consecutive_pindirs(pio, sm, AUDIO_I2S_LRCK, 2, true);
  pio_sm_set_consecutive_pindirs(pio, sm, AUDIO_I2S_DOUT, 1, true);

  pio_sm_config c = audio_playback_write_program_get_default_config(offset);
  sm_config_set_out_pins(&c, AUDIO_I2S_DOUT, 1);
  sm_config_set_sideset_pins(&c, AUDIO_I2S_LRCK);
  sm_config_set_out_shift(&c, false, false, AUDIO_BIT_DEPTH);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
  sm_config_set_clkdiv_int_frac8(&c, SM_CLKDIV_INT, SM_CLKDIV_FRAC);

  pio_sm_init(pio, sm, offset, &c);
  pio_sm_set_enabled(pio, sm, true);
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
  gpio_put(AUDIO_I2S_EN, true);

  uint8_t sm = pio_claim_unused_sm(AUDIO_I2S_PIO, true);

  audio_playback_write_pio_init(AUDIO_I2S_PIO, sm);
  audio_playback_write_dma_init(AUDIO_I2S_PIO, sm);
}

void audio_playback_init() {
  gpio_init(AUDIO_I2S_EN);
  gpio_set_dir(AUDIO_I2S_EN, GPIO_OUT);

  audio_buffer_pool_init(&pool, AUDIO_BUFFER_POOL_SIZE, AUDIO_BUFFER_SIZE);
  audio_playback_begin();
}

void audio_playback_run_forever(audio_synth_t *synth) {
  TimingInstrumenter ti_synth;

  int i = 0;
  int buf_per_sec = AUDIO_SAMPLE_RATE / AUDIO_BUFFER_SIZE;
  float ms_per_buf = 1000.0f / (float)buf_per_sec;
  while (true) {
    audio_buffer_t buffer = audio_buffer_pool_acquire_write(&pool, true);
    ti_start(&ti_synth);
    audio_synth_fill_buffer(synth, buffer, pool.buffer_size);
    ti_stop(&ti_synth);
    audio_buffer_pool_commit_write(&pool);

    if (i > buf_per_sec) {
      i = 0;
      printf("synth: %.2f ms / %.2f ms\n", ti_get_average_ms(&ti_synth, true),
             ms_per_buf);
    }
    i++;
  }
}
