#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "d.h"

#include "i2s.pio.h"

#define GPIO_DIN 26
#define GPIO_BCLK 27
#define GPIO_LRCLK 28

#define SYS_CLOCK_HZ 132000000
#define BIT_DEPTH 16
#define SAMPLE_RATE 32000
#define LOUDNESS 0.2f // Volume control for the sine wave

// Buffer size for double buffering (smaller chunks that fit in RAM)
#define BUFFER_SIZE 1024
#define NUM_BUFFERS 2

const uint32_t BCLK_HZ = SAMPLE_RATE * BIT_DEPTH * 2;
const uint32_t LRCLK_HZ = SAMPLE_RATE;
const uint32_t SM_BCLK = 2 * BCLK_HZ; // 2 clocks per bit

const uint32_t SM_CLKDIV_INT = SYS_CLOCK_HZ / SM_BCLK;
const uint32_t SM_CLKDIV_FRAC = (SYS_CLOCK_HZ % SM_BCLK) * 256 / SM_BCLK;

// Double buffers in RAM for streaming from flash
uint32_t audio_buffers[NUM_BUFFERS][BUFFER_SIZE];
volatile uint32_t current_buffer = 0;
volatile uint32_t wav_position = 0;
volatile bool buffer_ready[NUM_BUFFERS] = {false, false};

int dma_chan;

// Fill a buffer with WAV data from flash
void fill_buffer(uint32_t buffer_index) {
  uint32_t samples_to_copy = BUFFER_SIZE;

  // Check if we're near the end of the WAV data
  if (wav_position + BUFFER_SIZE > WAV_DATA_LENGTH) {
    samples_to_copy = WAV_DATA_LENGTH - wav_position;
    // Fill remaining with zeros or loop back to beginning
    for (uint32_t i = samples_to_copy; i < BUFFER_SIZE; i++) {
      audio_buffers[buffer_index][i] = 0;
    }
  }

  // Copy data from flash to RAM buffer
  memcpy(audio_buffers[buffer_index], &WAV_DATA[wav_position],
         samples_to_copy * sizeof(uint32_t));

  wav_position += samples_to_copy;

  // Loop back to beginning if we've reached the end
  if (wav_position >= WAV_DATA_LENGTH) {
    wav_position = 0;
  }

  buffer_ready[buffer_index] = true;
}

void __isr dma_handler() {
  dma_hw->ints0 = 1u << dma_chan; // clear IRQ

  // Switch to the next buffer
  uint32_t next_buffer = (current_buffer + 1) % NUM_BUFFERS;

  // Start DMA with the next buffer if it's ready
  if (buffer_ready[next_buffer]) {
    dma_channel_set_read_addr(dma_chan, audio_buffers[next_buffer], true);
    current_buffer = next_buffer;
    buffer_ready[current_buffer] = false;

    // Prepare the buffer that just finished for the next cycle
    uint32_t buffer_to_fill = (current_buffer + 1) % NUM_BUFFERS;
    fill_buffer(buffer_to_fill);
  }
}

int main() {
  set_sys_clock_hz(SYS_CLOCK_HZ, true);
  stdio_init_all();

  printf("System Clock: %lu\n", clock_get_hz(clk_sys));
  printf("WAV Data Length: %d samples\n", WAV_DATA_LENGTH);
  printf("Buffer Size: %d samples\n", BUFFER_SIZE);

  // Initialize the first two buffers
  fill_buffer(0);
  fill_buffer(1);

  int sm = pio_claim_unused_sm(pio0, true);
  uint offset = pio_add_program(pio0, &i2s_out_master_program);
  i2s_out_master_program_init(pio0, sm, offset, BIT_DEPTH, GPIO_DIN, GPIO_BCLK);

  pio_sm_set_clkdiv_int_frac8(pio0, sm, SM_CLKDIV_INT, SM_CLKDIV_FRAC);
  pio_sm_set_enabled(pio0, sm, true);

  // Setup DMA for double buffering
  dma_chan = dma_claim_unused_channel(true);
  dma_channel_config dcfg = dma_channel_get_default_config(dma_chan);
  channel_config_set_transfer_data_size(&dcfg, DMA_SIZE_32);
  channel_config_set_dreq(&dcfg, pio_get_dreq(pio0, sm, true));
  channel_config_set_read_increment(&dcfg, true);
  channel_config_set_write_increment(&dcfg, false);

  // Configure DMA to transfer from first buffer
  dma_channel_configure(dma_chan, &dcfg,
                        &pio0->txf[sm],   // write to: PIO TX FIFO
                        audio_buffers[0], // read from: first audio buffer
                        BUFFER_SIZE,      // transfer count
                        false             // don't start yet
  );

  // Enable DMA interrupts
  dma_channel_set_irq0_enabled(dma_chan, true);
  irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
  irq_set_enabled(DMA_IRQ_0, true);

  // Mark first buffer as in use and start DMA
  buffer_ready[0] = false;
  dma_channel_start(dma_chan);

  printf("Starting WAV playback...\n");

  while (1) {
    __wfi();
  }
}
