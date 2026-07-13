#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <platform/display.h>
#include <prism/graphics/layout.h>
#include <shared/audio/synth_internal.h>
#include <shared/engine.h>

#include "midi_mode.h"

enum
{
  FFT_SIZE = AUDIO_SYNTH_ANALYSIS_SAMPLE_COUNT,
  FFT_LOG2_SIZE = 10,
  FFT_BAND_COUNT = 24,
  FFT_PREPARE_SAMPLES_PER_TICK = 64,
  FFT_UPDATE_TICKS = 64,
  SPECTRUM_TOP = 21,
  SPECTRUM_BOTTOM = 57,
  SPECTRUM_HEIGHT = SPECTRUM_BOTTOM - SPECTRUM_TOP,
  VU_SEGMENT_COUNT = 10,
  VU_SEGMENT_FACE_WIDTH = 8,
  VU_SEGMENT_PITCH = 10,
  VU_Y = 59,
  VU_HEIGHT = 5,
  SPECTRUM_PEAK_DECAY_FRAMES = 12,
  VU_DECAY_FRAMES = 12,
  QUEUE_WINDOW_SAMPLES = 32,
  QUEUE_WINDOW_TICKS = PRISM_ENGINE_TICK_RATE / 5u,
  WARNING_HOLD_TICKS = PRISM_ENGINE_TICK_RATE,
};

_Static_assert(FFT_SIZE == (1 << FFT_LOG2_SIZE),
               "MIDI analyzer FFT size must be a power of two");

typedef enum
{
  ANALYZER_IDLE,
  ANALYZER_PREPARE,
  ANALYZER_FFT,
  ANALYZER_BANDS,
} analyzer_phase_t;

typedef struct
{
  analyzer_phase_t phase;
  uint32_t tick;
  uint32_t next_update_tick;
  uint32_t sample_sequence;
  uint16_t prepare_index;
  uint8_t fft_stage;
  uint8_t decay_phase;
  uint8_t peak_decay_phase;
  uint8_t vu_decay_phase;
  uint8_t spectrum[FFT_BAND_COUNT];
  uint8_t spectrum_target[FFT_BAND_COUNT];
  uint8_t spectrum_peak[FFT_BAND_COUNT];
  uint8_t vu_level;
  uint8_t queue_level;
  uint8_t queue_sample_index;
  uint8_t queue_samples[QUEUE_WINDOW_SAMPLES];
  uint32_t queue_sample_ticks[QUEUE_WINDOW_SAMPLES];
  uint32_t late_count;
  uint32_t underrun_count;
  uint32_t queue_full_count;
  uint32_t late_until_tick;
  uint32_t pop_until_tick;
  uint32_t full_until_tick;
  int16_t samples[FFT_SIZE];
  int16_t real[FFT_SIZE];
  int16_t imaginary[FFT_SIZE];
} midi_analyzer_t;

typedef struct
{
  midi_analyzer_t analyzer;
  int16_t twiddle_real[FFT_SIZE / 2];
  int16_t twiddle_imaginary[FFT_SIZE / 2];
  audio_synth_analysis_buffers_t capture;
} midi_mode_state_t;

static midi_mode_state_t *midi_state;

#if defined(PRISM_TESTING)
static bool test_allocation_failure;
#endif

#define analyzer (midi_state->analyzer)
#define twiddle_real (midi_state->twiddle_real)
#define twiddle_imaginary (midi_state->twiddle_imaginary)

/* Each pair brackets one logarithmic display band. The 94 Hz to 8 kHz range
 * keeps the useful musical and built-in-speaker spectrum spread across the
 * display instead of spending columns on near-DC or inaudible harmonics. */
static const uint16_t band_edges[FFT_BAND_COUNT + 1] = {
    2,  3,  4,  5,  6,   7,   8,   9,   10,  11,  13, 15, 18,
    22, 27, 32, 39, 47,  56,  68,  81,  98,  118, 142, 171,
};

static int16_t q31_to_q15(int32_t value)
{
  int32_t shifted = value >> 16;
  if (shifted > INT16_MAX)
    return INT16_MAX;
  if (shifted < INT16_MIN)
    return INT16_MIN;
  return (int16_t)shifted;
}

static void initialize_twiddles(void)
{
  /* Unit rotation by -2*pi/1024 in Q1.31. Building the table on entry avoids
   * floating point and trigonometry in both initialization and the FFT. */
  const int32_t step_real = 2147443221;
  const int32_t step_imaginary = -13176712;
  int32_t real = INT32_MAX;
  int32_t imaginary = 0;
  for (uint16_t i = 0; i < FFT_SIZE / 2; i++)
  {
    twiddle_real[i] = q31_to_q15(real);
    twiddle_imaginary[i] = q31_to_q15(imaginary);
    int32_t next_real =
        (int32_t)(((int64_t)real * step_real -
                   (int64_t)imaginary * step_imaginary) >>
                  31);
    int32_t next_imaginary =
        (int32_t)(((int64_t)real * step_imaginary +
                   (int64_t)imaginary * step_real) >>
                  31);
    real = next_real;
    imaginary = next_imaginary;
  }
}

static uint16_t reverse_fft_bits(uint16_t value)
{
  uint16_t reversed = 0;
  for (uint8_t bit = 0; bit < FFT_LOG2_SIZE; bit++)
  {
    reversed = (uint16_t)((reversed << 1) | (value & 1u));
    value >>= 1;
  }
  return reversed;
}

static void analyzer_prepare_chunk(void)
{
  uint16_t end = analyzer.prepare_index + FFT_PREPARE_SAMPLES_PER_TICK;
  if (end > FFT_SIZE)
    end = FFT_SIZE;

  for (uint16_t i = analyzer.prepare_index; i < end; i++)
  {
    /* A Bartlett window is cheap in fixed point and suppresses the severe
     * leakage a rectangular window would create on the tiny 32-band view. */
    uint16_t distance = i < FFT_SIZE / 2 ? i : (FFT_SIZE - 1u - i);
    int16_t window = (int16_t)(distance * 64u);
    uint16_t destination = reverse_fft_bits(i);
    analyzer.real[destination] =
        (int16_t)(((int32_t)analyzer.samples[i] * window) >> 15);
    analyzer.imaginary[destination] = 0;
  }

  analyzer.prepare_index = end;
  if (end == FFT_SIZE)
  {
    analyzer.phase = ANALYZER_FFT;
    analyzer.fft_stage = 1;
  }
}

static void analyzer_fft_stage(void)
{
  uint16_t length = (uint16_t)(1u << analyzer.fft_stage);
  uint16_t half = length / 2u;
  uint16_t twiddle_step = FFT_SIZE / length;

  for (uint16_t base = 0; base < FFT_SIZE; base += length)
  {
    uint16_t twiddle = 0;
    for (uint16_t offset = 0; offset < half; offset++)
    {
      uint16_t even = base + offset;
      uint16_t odd = even + half;
      int32_t odd_real = analyzer.real[odd];
      int32_t odd_imaginary = analyzer.imaginary[odd];
      int32_t product_real =
          ((int32_t)twiddle_real[twiddle] * odd_real -
           (int32_t)twiddle_imaginary[twiddle] * odd_imaginary) >>
          15;
      int32_t product_imaginary =
          ((int32_t)twiddle_real[twiddle] * odd_imaginary +
           (int32_t)twiddle_imaginary[twiddle] * odd_real) >>
          15;
      int32_t even_real = analyzer.real[even];
      int32_t even_imaginary = analyzer.imaginary[even];

      /* Scale each stage by two. This prevents overflow and leaves the final
       * transform normalized by FFT_SIZE. */
      analyzer.real[even] = (int16_t)((even_real + product_real) >> 1);
      analyzer.imaginary[even] =
          (int16_t)((even_imaginary + product_imaginary) >> 1);
      analyzer.real[odd] = (int16_t)((even_real - product_real) >> 1);
      analyzer.imaginary[odd] =
          (int16_t)((even_imaginary - product_imaginary) >> 1);
      twiddle += twiddle_step;
    }
  }

  if (analyzer.fft_stage == FFT_LOG2_SIZE)
    analyzer.phase = ANALYZER_BANDS;
  else
    analyzer.fft_stage++;
}

static uint8_t logarithmic_extent(uint32_t magnitude, uint8_t extent,
                                  uint8_t floor_log2, uint8_t ceiling_log2)
{
  uint32_t floor_value = 1u << floor_log2;
  if (magnitude < floor_value)
    return 0;

  uint32_t ceiling_value = 1u << ceiling_log2;
  if (magnitude >= ceiling_value)
    return extent;

  uint8_t whole = (uint8_t)(31u - (uint8_t)__builtin_clz(magnitude));
  uint32_t base = 1u << whole;
  uint32_t fraction = ((magnitude - base) << 8) / base;
  uint32_t level = ((uint32_t)(whole - floor_log2) << 8) + fraction;
  uint32_t range = (uint32_t)(ceiling_log2 - floor_log2) << 8;
  return (uint8_t)((level * extent + range / 2u) / range);
}

static void analyzer_update_bands(void)
{
  for (uint8_t band = 0; band < FFT_BAND_COUNT; band++)
  {
    uint32_t maximum = 0;
    for (uint16_t bin = band_edges[band]; bin < band_edges[band + 1]; bin++)
    {
      int32_t real = analyzer.real[bin];
      int32_t imaginary = analyzer.imaginary[bin];
      uint32_t magnitude = (uint32_t)(real < 0 ? -real : real) +
                           (uint32_t)(imaginary < 0 ? -imaginary : imaginary);
      if (magnitude > maximum)
        maximum = magnitude;
    }
    analyzer.spectrum_target[band] =
        logarithmic_extent(maximum, SPECTRUM_HEIGHT, 3, 13);
  }
  analyzer.phase = ANALYZER_IDLE;
}

static void enter(void)
{
#if defined(PRISM_TESTING)
  if (test_allocation_failure)
  {
    midi_state = NULL;
    return;
  }
#endif
  midi_state = calloc(1, sizeof(*midi_state));
  if (midi_state == NULL)
    return;
  initialize_twiddles();
  analyzer.late_count = audio_synth_analysis_late_count(engine_synth());
  analyzer.underrun_count =
      audio_synth_analysis_underrun_count(engine_synth());
  analyzer.queue_full_count =
      audio_synth_message_queue_full_count(engine_synth());
  if (!audio_synth_analysis_enable(engine_synth(), &midi_state->capture))
  {
    free(midi_state);
    midi_state = NULL;
  }
}

static void leave(void)
{
  if (midi_state == NULL)
    return;
  audio_synth_analysis_disable_sync(engine_synth());
  free(midi_state);
  midi_state = NULL;
}

static void tick(void)
{
  /* MIDI synth is an OS mode, not an idle application: it must remain awake
   * even when the controller leaves a note sustaining without more traffic. */
  engine_mark_input();
  if (midi_state == NULL)
    return;

  analyzer.tick++;
  uint32_t late_count = audio_synth_analysis_late_count(engine_synth());
  if (late_count != analyzer.late_count)
  {
    analyzer.late_count = late_count;
    analyzer.late_until_tick = analyzer.tick + WARNING_HOLD_TICKS;
  }
  uint32_t underrun_count =
      audio_synth_analysis_underrun_count(engine_synth());
  if (underrun_count != analyzer.underrun_count)
  {
    analyzer.underrun_count = underrun_count;
    analyzer.pop_until_tick = analyzer.tick + WARNING_HOLD_TICKS;
  }
  uint32_t queue_full_count =
      audio_synth_message_queue_full_count(engine_synth());
  if (queue_full_count != analyzer.queue_full_count)
  {
    analyzer.queue_full_count = queue_full_count;
    analyzer.full_until_tick = analyzer.tick + WARNING_HOLD_TICKS;
  }

  if (analyzer.phase == ANALYZER_IDLE &&
      analyzer.tick >= analyzer.next_update_tick &&
      audio_synth_analysis_snapshot(engine_synth(), analyzer.samples,
                                    &analyzer.sample_sequence))
  {
    analyzer.prepare_index = 0;
    analyzer.phase = ANALYZER_PREPARE;
    analyzer.next_update_tick = analyzer.tick + FFT_UPDATE_TICKS;
  }

  switch (analyzer.phase)
  {
  case ANALYZER_PREPARE:
    analyzer_prepare_chunk();
    break;
  case ANALYZER_FFT:
    analyzer_fft_stage();
    break;
  case ANALYZER_BANDS:
    analyzer_update_bands();
    break;
  case ANALYZER_IDLE:
    break;
  }
}

static void update_display_levels(void)
{
  analyzer.decay_phase ^= 1u;
  if (++analyzer.peak_decay_phase >= SPECTRUM_PEAK_DECAY_FRAMES)
    analyzer.peak_decay_phase = 0;
  if (++analyzer.vu_decay_phase >= VU_DECAY_FRAMES)
    analyzer.vu_decay_phase = 0;
  for (uint8_t band = 0; band < FFT_BAND_COUNT; band++)
  {
    uint8_t target = analyzer.spectrum_target[band];
    if (target >= analyzer.spectrum[band])
      analyzer.spectrum[band] = target;
    else if (analyzer.decay_phase == 0)
      analyzer.spectrum[band]--;

    if (target >= analyzer.spectrum_peak[band])
      analyzer.spectrum_peak[band] = target;
    else if (analyzer.peak_decay_phase == 0 &&
             analyzer.spectrum_peak[band] != 0)
      analyzer.spectrum_peak[band]--;
  }

  uint16_t peak = audio_synth_analysis_take_peak(engine_synth());
  uint8_t target = logarithmic_extent(peak, VU_SEGMENT_COUNT, 5, 15);
  if (target >= analyzer.vu_level)
    analyzer.vu_level = target;
  else if (analyzer.vu_decay_phase == 0)
    analyzer.vu_level--;

  uint16_t queue_peak =
      audio_synth_message_queue_take_peak(engine_synth());
  if (queue_peak >= AUDIO_SYNTH_MESSAGE_QUEUE_SIZE)
    analyzer.full_until_tick = analyzer.tick + WARNING_HOLD_TICKS;
  if (queue_peak > UINT8_MAX)
    queue_peak = UINT8_MAX;
  analyzer.queue_samples[analyzer.queue_sample_index] = (uint8_t)queue_peak;
  analyzer.queue_sample_ticks[analyzer.queue_sample_index] = analyzer.tick;
  analyzer.queue_sample_index =
      (uint8_t)((analyzer.queue_sample_index + 1u) % QUEUE_WINDOW_SAMPLES);

  analyzer.queue_level = 0;
  for (uint8_t i = 0; i < QUEUE_WINDOW_SAMPLES; i++)
  {
    uint32_t age = analyzer.tick - analyzer.queue_sample_ticks[i];
    if (age <= QUEUE_WINDOW_TICKS &&
        analyzer.queue_samples[i] > analyzer.queue_level)
      analyzer.queue_level = analyzer.queue_samples[i];
  }
}

static void draw_header(u8g2_t *u8g2)
{
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  u8g2_DrawStr(u8g2, 0, 7, "synth");

  uint16_t active = audio_synth_active_voice_mask(engine_synth());
  for (uint8_t voice = 0; voice < AUDIO_SYNTH_VOICE_COUNT; voice++)
  {
    uint8_t x = (uint8_t)(45u + voice * 7u);
    if ((active & (1u << voice)) != 0)
      u8g2_DrawBox(u8g2, x, 2, 5, 5);
    else
      u8g2_DrawFrame(u8g2, x, 2, 5, 5);
  }

  char queue[8];
  snprintf(queue, sizeof(queue), "%02u/%u",
           (unsigned int)(analyzer.queue_level > 99 ? 99
                                                    : analyzer.queue_level),
           AUDIO_SYNTH_MESSAGE_QUEUE_SIZE);
  u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
  elm_t root = elm_root(u8g2, VEC2_Z);
  const elm_insets_t status_padding = {
      .top = 1, .right = 2, .bottom = 2, .left = 2};
  elm_boxed_text(&root, vec2(0, 9), queue, ELM_ALIGN_TOP_LEFT, 6,
                 status_padding, analyzer.tick < analyzer.full_until_tick);
  elm_boxed_text(&root, vec2(108, 9), "late", ELM_ALIGN_TOP_RIGHT, 6,
                 status_padding,
                 analyzer.tick < analyzer.late_until_tick);
  elm_boxed_text(&root, vec2(128, 9), "pop", ELM_ALIGN_TOP_RIGHT, 6,
                 status_padding,
                 analyzer.tick < analyzer.pop_until_tick);
}

static void draw_spectrum(u8g2_t *u8g2)
{
  for (uint8_t band = 0; band < FFT_BAND_COUNT; band++)
  {
    uint8_t height = analyzer.spectrum[band];
    uint8_t x = (uint8_t)(4u + band * 5u);
    if (height != 0)
      u8g2_DrawBox(u8g2, x, SPECTRUM_BOTTOM - height, 4, height);

    uint8_t peak = analyzer.spectrum_peak[band];
    if (peak > height + 1u)
      u8g2_DrawHLine(u8g2, x, SPECTRUM_BOTTOM - peak, 4);
  }
}

static void draw_vu(u8g2_t *u8g2)
{
  for (uint8_t segment = 0; segment < VU_SEGMENT_COUNT; segment++)
  {
    uint8_t x = (uint8_t)(segment * VU_SEGMENT_PITCH);
    bool filled = segment < analyzer.vu_level;
    for (uint8_t row = 0; row < VU_HEIGHT; row++)
    {
      uint8_t shift = (uint8_t)(VU_HEIGHT - 1u - row);
      uint8_t row_x = x + shift;
      if (filled || row == 0 || row == VU_HEIGHT - 1u)
        u8g2_DrawHLine(u8g2, row_x, VU_Y + row,
                       VU_SEGMENT_FACE_WIDTH);
      else
      {
        u8g2_DrawPixel(u8g2, row_x, VU_Y + row);
        u8g2_DrawPixel(u8g2, row_x + VU_SEGMENT_FACE_WIDTH - 1u,
                       VU_Y + row);
      }
    }
  }

  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  u8g2_DrawStr(u8g2, 108, 63, "peak");
}

static void frame(void)
{
  u8g2_t *u8g2 = platform_display_get_u8g2();
  u8g2_SetDrawColor(u8g2, 1);
  if (midi_state == NULL)
  {
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    u8g2_DrawStr(u8g2, 0, 7, "synth");
    uint16_t active = audio_synth_active_voice_mask(engine_synth());
    for (uint8_t voice = 0; voice < AUDIO_SYNTH_VOICE_COUNT; voice++)
    {
      uint8_t x = (uint8_t)(45u + voice * 7u);
      if ((active & (1u << voice)) != 0)
        u8g2_DrawBox(u8g2, x, 2, 5, 5);
      else
        u8g2_DrawFrame(u8g2, x, 2, 5, 5);
    }
    u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
    u8g2_DrawStr(u8g2, 0, 17, "analysis unavailable");
    return;
  }
  update_display_levels();
  draw_header(u8g2);
  draw_spectrum(u8g2);
  draw_vu(u8g2);
}

static app_t app_midi_mode = {
    .name = "midi synth",
    .enter = enter,
    .tick = tick,
    .frame = frame,
    .leave = leave,
};

bool prism_midi_mode_active(void) { return engine_is_app(&app_midi_mode); }

void prism_midi_mode_enter(void)
{
  if (!prism_midi_mode_active())
    engine_set_app(&app_midi_mode);
  engine_mark_input();
}

void prism_midi_mode_usb_disconnected(void)
{
  if (prism_midi_mode_active())
    engine_set_app(NULL);
}

#if defined(PRISM_TESTING)
void prism_midi_mode_test_set_allocation_failure(bool fail)
{
  test_allocation_failure = fail;
}

void prism_midi_mode_test_enter(void) { enter(); }
void prism_midi_mode_test_leave(void) { leave(); }
size_t prism_midi_mode_test_allocation_bytes(void)
{
  return midi_state == NULL ? 0 : sizeof(*midi_state);
}
#endif
