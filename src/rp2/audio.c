#include <hardware/dma.h>
#include <hardware/pio.h>
#include <hardware/sync.h>

#include "config.h"

#include <shared/audio/buffer.h>
#include <shared/audio/synth_internal.h>
#if PRISM_ENABLE_PERFORMANCE_LOGS
#include <stdio.h>
#include <shared/utils/timing.h>
#endif

#include "audio.h"
#include "audio.pio.h"

static const uint32_t SM_CLOCK_HZ =
    AUDIO_SAMPLE_RATE * AUDIO_BIT_DEPTH * 2u * 2u;

static const uint32_t SM_CLKDIV_INT = SYS_CLOCK_HZ / SM_CLOCK_HZ;
static const uint32_t SM_CLKDIV_FRAC =
    (SYS_CLOCK_HZ % SM_CLOCK_HZ) * 256 / SM_CLOCK_HZ;

/* DMA masters are not paused by multicore flash lockout. Keep the fallback
 * source in SRAM so an audio underrun cannot make DMA touch XIP while core 0
 * is erasing or programming cartridge flash. */
static uint32_t silent_buffer[AUDIO_BUFFER_SIZE];
static uint32_t audio_buffers[AUDIO_BUFFER_RING_SLOTS][AUDIO_BUFFER_SIZE];
static uint8_t ring_write;
static uint8_t ring_read;
static int dma_channel;
static uint8_t pio_sm;
static bool dma_using_ring_buffer;

enum
{
  AUDIO_RUNNING,
  AUDIO_SUSPEND_REQUESTED,
  AUDIO_SUSPENDED,
  AUDIO_RESUME_REQUESTED,
};

static uint8_t audio_state = AUDIO_RUNNING;

static inline uint8_t ring_next(uint8_t index)
{
  return (uint8_t)((index + 1u) & (AUDIO_BUFFER_RING_SLOTS - 1u));
}

static audio_buffer_t ring_acquire_write(void)
{
  uint8_t write = __atomic_load_n(&ring_write, __ATOMIC_RELAXED);
  uint8_t next = ring_next(write);
  while (next == __atomic_load_n(&ring_read, __ATOMIC_ACQUIRE))
    __wfi();
  return audio_buffers[write];
}

static void ring_commit_write(void)
{
  uint8_t write = __atomic_load_n(&ring_write, __ATOMIC_RELAXED);
  __atomic_store_n(&ring_write, ring_next(write), __ATOMIC_RELEASE);
}

static audio_buffer_t ring_acquire_read(void)
{
  uint8_t read = __atomic_load_n(&ring_read, __ATOMIC_RELAXED);
  if (read == __atomic_load_n(&ring_write, __ATOMIC_ACQUIRE))
    return NULL;
  return audio_buffers[read];
}

static void ring_commit_read(void)
{
  uint8_t read = __atomic_load_n(&ring_read, __ATOMIC_RELAXED);
  __atomic_store_n(&ring_read, ring_next(read), __ATOMIC_RELEASE);
}

static void audio_playback_write_pio_init(PIO pio, uint8_t sm)
{
  int offset = pio_add_program(pio, &audio_playback_write_program);
  if (offset < 0)
  {
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

static void __isr audio_playback_write_dma_irq_handler(void)
{
  dma_hw->ints0 = 1u << dma_channel;

  if (dma_using_ring_buffer)
    ring_commit_read();

  audio_buffer_t next_buffer = ring_acquire_read();
  if (next_buffer == NULL)
  {
    dma_channel_set_read_addr(dma_channel, silent_buffer, true);
    dma_using_ring_buffer = false;
  }
  else
  {
    dma_channel_set_read_addr(dma_channel, next_buffer, true);
    dma_using_ring_buffer = true;
  }
}

static void audio_playback_write_dma_init(PIO pio, uint8_t sm)
{
  dma_channel = dma_claim_unused_channel(true);
  dma_channel_config c = dma_channel_get_default_config(dma_channel);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);

  dma_channel_configure(dma_channel, &c, &pio->txf[sm], silent_buffer,
                        AUDIO_BUFFER_SIZE, false);

  dma_channel_set_irq0_enabled(dma_channel, true);
  irq_set_exclusive_handler(DMA_IRQ_0, audio_playback_write_dma_irq_handler);
  irq_set_enabled(DMA_IRQ_0, true);

  dma_channel_start(dma_channel);
}

static void audio_hardware_suspend(void)
{
  dma_channel_set_irq0_enabled(dma_channel, false);
  pio_sm_set_enabled(AUDIO_I2S_PIO, pio_sm, false);
  dma_channel_abort(dma_channel);
  dma_hw->ints0 = 1u << dma_channel;
  dma_using_ring_buffer = false;
  __atomic_store_n(&ring_read, 0, __ATOMIC_RELAXED);
  __atomic_store_n(&ring_write, 0, __ATOMIC_RELAXED);
}

static void audio_hardware_resume(void)
{
  pio_sm_clear_fifos(AUDIO_I2S_PIO, pio_sm);
  pio_sm_restart(AUDIO_I2S_PIO, pio_sm);
  pio_sm_clkdiv_restart(AUDIO_I2S_PIO, pio_sm);

  dma_channel_set_read_addr(dma_channel, silent_buffer, false);
  dma_channel_set_trans_count(dma_channel, AUDIO_BUFFER_SIZE, false);
  dma_hw->ints0 = 1u << dma_channel;
  dma_channel_set_irq0_enabled(dma_channel, true);
  dma_channel_start(dma_channel);
  pio_sm_set_enabled(AUDIO_I2S_PIO, pio_sm, true);
}

static void __no_inline_not_in_flash_func(audio_core_park)(void)
{
  uint32_t interrupts = save_and_disable_interrupts();
  __atomic_store_n(&audio_state, AUDIO_SUSPENDED, __ATOMIC_RELEASE);
  __sev();
  while (__atomic_load_n(&audio_state, __ATOMIC_ACQUIRE) !=
         AUDIO_RESUME_REQUESTED)
    __wfe();
  restore_interrupts(interrupts);
}

static void audio_core_suspend(void)
{
  audio_hardware_suspend();
  audio_core_park();
  audio_hardware_resume();
  __atomic_store_n(&audio_state, AUDIO_RUNNING, __ATOMIC_RELEASE);
  __sev();
}

static bool audio_suspend_requested(void)
{
  return __atomic_load_n(&audio_state, __ATOMIC_ACQUIRE) ==
         AUDIO_SUSPEND_REQUESTED;
}

void audio_playback_set_enabled(bool enabled)
{
  gpio_put(AUDIO_I2S_EN, enabled);
}

void audio_playback_suspend(void)
{
  gpio_put(AUDIO_I2S_EN, false);
  __atomic_store_n(&audio_state, AUDIO_SUSPEND_REQUESTED, __ATOMIC_RELEASE);
  while (__atomic_load_n(&audio_state, __ATOMIC_ACQUIRE) != AUDIO_SUSPENDED)
    __wfe();
}

void audio_playback_resume(void)
{
  __atomic_store_n(&audio_state, AUDIO_RESUME_REQUESTED, __ATOMIC_RELEASE);
  __sev();
  while (__atomic_load_n(&audio_state, __ATOMIC_ACQUIRE) != AUDIO_RUNNING)
    __wfe();
  gpio_put(AUDIO_I2S_EN, true);
}

void audio_playback_init()
{
  gpio_init(AUDIO_I2S_EN);
  gpio_set_dir(AUDIO_I2S_EN, GPIO_OUT);
  gpio_put(AUDIO_I2S_EN, true);

  pio_sm = pio_claim_unused_sm(AUDIO_I2S_PIO, true);

  audio_playback_write_pio_init(AUDIO_I2S_PIO, pio_sm);
  audio_playback_write_dma_init(AUDIO_I2S_PIO, pio_sm);
  __atomic_store_n(&audio_state, AUDIO_RUNNING, __ATOMIC_RELEASE);
}

void audio_playback_run_forever(audio_synth_t *synth)
{
#if PRISM_ENABLE_PERFORMANCE_LOGS
  TimingInstrumenter ti_synth;
  ti_init(&ti_synth);

  uint32_t buffers_since_log = 0;
  const uint32_t buffers_per_second = AUDIO_SAMPLE_RATE / AUDIO_BUFFER_SIZE;
  const float buffer_budget_ms = 1000.0f / (float)buffers_per_second;
#endif

  while (true)
  {
    if (audio_suspend_requested())
    {
      audio_core_suspend();
      continue;
    }

    audio_buffer_t buffer = ring_acquire_write();
    if (audio_suspend_requested())
    {
      audio_core_suspend();
      continue;
    }
#if PRISM_ENABLE_PERFORMANCE_LOGS
    ti_start(&ti_synth);
#endif
    audio_synth_fill_buffer(synth, buffer, AUDIO_BUFFER_SIZE);
#if PRISM_ENABLE_PERFORMANCE_LOGS
    uint64_t elapsed_us = ti_stop(&ti_synth);
#endif
    ring_commit_write();

#if PRISM_ENABLE_PERFORMANCE_LOGS
    if (elapsed_us > (uint64_t)(buffer_budget_ms * 1000.0f))
    {
      printf("synth warning: buffer generation took %.2f ms (%.2f ms budget)\n",
             elapsed_us / 1000.0f, buffer_budget_ms);
    }

    if (++buffers_since_log >= buffers_per_second)
    {
      buffers_since_log = 0;
      printf("synth: %.2f ms / %.2f ms\n", ti_get_average_ms(&ti_synth, true),
             buffer_budget_ms);
    }
#endif
  }
}
