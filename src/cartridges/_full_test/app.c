#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <prism/sdk.h>

#define WORKLOAD_ITERATIONS 50000u
#define FPS_WINDOW_US 1000000u
#define AUTO_NOTE_INTERVAL_TICKS (PRISM_ENGINE_TICK_RATE / 4u)
#define AUTO_NOTE_DURATION_TICKS (PRISM_ENGINE_TICK_RATE / 10u)

static const uint8_t diagnostic_icon[PRISM_CARTRIDGE_ICON_BYTES] = {
    0xff, 0xff, 0xff, 0xff, 0x0f,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0xff, 0xff, 0x0f, 0x08,
    0x01, 0xff, 0xff, 0x0f, 0x08,
    0x01, 0xff, 0xff, 0x0f, 0x08,
    0x01, 0xff, 0xff, 0x0f, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x0f, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0x01, 0x00, 0x00, 0x00, 0x08,
    0xff, 0xff, 0xff, 0xff, 0x0f,
};

typedef struct
{
  prism_time_t fps_window_started;
  uint32_t frame_count;
  uint32_t fps;
  uint32_t work_sink;
  uint32_t next_auto_note_tick;
  uint32_t auto_note_off_tick;
  uint8_t auto_note;
  bool auto_note_active;
  uint16_t button_count[3];
} state_t;

static state_t *state;

#if defined(PRISM_TESTING)
static bool test_allocation_failure;
#endif

static void synth_note(prism_t *prism, uint8_t patch, uint8_t note,
                       bool pressed)
{
  audio_synth_message_t message = {
      .type = pressed ? AUDIO_SYNTH_MESSAGE_NOTE_ON
                      : AUDIO_SYNTH_MESSAGE_NOTE_OFF,
  };
  if (pressed)
  {
    message.data.note_on.patch_idx = patch;
    message.data.note_on.note_number = note;
    message.data.note_on.velocity = 100;
  }
  else
  {
    message.data.note_off.patch_idx = patch;
    message.data.note_off.note_number = (int8_t)note;
  }
  audio_synth_enqueue(prism_synth(prism), &message);
}

static audio_synth_patch_config_t diagnostic_patch(void)
{
  audio_synth_patch_config_t patch = audio_synth_patch_config_default;
  patch.ops[0].env = (audio_synth_env_config_t){
      .a = 2,
      .d = 140,
      .s = q1x31_f(0.18f),
      .r = 80,
  };
  patch.ops[0].freq_mult = 1;
  patch.ops[0].level = q1x15_f(0.35f);
  patch.ops[1].env = (audio_synth_env_config_t){
      .a = 0,
      .d = 90,
      .s = q1x31_f(0.0f),
      .r = 60,
  };
  patch.ops[1].freq_mult = 3;
  patch.ops[1].level = q1x15_f(0.22f);
  patch.ops[1].mode = AUDIO_SYNTH_OP_MODE_FREQ_MOD;
  return patch;
}

static void enter(prism_t *prism)
{
#if defined(PRISM_TESTING)
  if (test_allocation_failure)
  {
    state = NULL;
    return;
  }
#endif
  state = calloc(1, sizeof(*state));
  if (state == NULL)
    return;
  state->fps_window_started = prism_now_us(prism);
  state->next_auto_note_tick = prism_ticks(prism);
  state->auto_note = 60;
  audio_synth_patch_config_t patch = diagnostic_patch();
  audio_synth_patch_config_set(prism_synth(prism), 0, patch);
  audio_synth_patch_config_set(prism_synth(prism), 1, patch);
}

static void tick(prism_t *prism)
{
  static const uint8_t button_notes[] = {48, 60, 72};
  if (state == NULL)
    return;
  uint32_t now = prism_ticks(prism);

  if (state->auto_note_active &&
      (int32_t)(now - state->auto_note_off_tick) >= 0)
  {
    synth_note(prism, 0, state->auto_note, false);
    state->auto_note_active = false;
  }
  if ((int32_t)(now - state->next_auto_note_tick) >= 0)
  {
    static const uint8_t sequence[] = {60, 64, 67, 72, 67, 64};
    uint32_t step = (now / AUTO_NOTE_INTERVAL_TICKS) %
                    (sizeof(sequence) / sizeof(sequence[0]));
    state->auto_note = sequence[step];
    synth_note(prism, 0, state->auto_note, true);
    state->auto_note_active = true;
    state->auto_note_off_tick = now + AUTO_NOTE_DURATION_TICKS;
    state->next_auto_note_tick = now + AUTO_NOTE_INTERVAL_TICKS;
  }

  for (prism_button_t button = PRISM_BUTTON_LEFT;
       button <= PRISM_BUTTON_MENU; ++button)
  {
    uint8_t index = button - PRISM_BUTTON_LEFT;
    if (prism_button_keydown(prism, button))
    {
      ++state->button_count[index];
      synth_note(prism, 1, button_notes[index], true);
    }
    if (prism_button_keyup(prism, button))
      synth_note(prism, 1, button_notes[index], false);
  }
}

static prism_color_t color_wheel(uint8_t position)
{
  if (position < 85)
    return prism_rgba((uint8_t)(255 - position * 3),
                      (uint8_t)(position * 3), 0, 255);
  if (position < 170)
  {
    position = (uint8_t)(position - 85);
    return prism_rgba(0, (uint8_t)(255 - position * 3),
                      (uint8_t)(position * 3), 255);
  }
  position = (uint8_t)(position - 170);
  return prism_rgba((uint8_t)(position * 3), 0,
                    (uint8_t)(255 - position * 3), 255);
}

static uint32_t run_workload(uint32_t value)
{
  for (uint32_t i = 1; i <= WORKLOAD_ITERATIONS; ++i)
  {
    value = value * 1664525u + 1013904223u;
    value ^= value >> 13;
    value += value / ((i & 31u) + 1u);
    value ^= value << 7;
  }
  return value;
}

static void draw_button(u8g2_t *u8g2, int16_t x, const char *label,
                        uint16_t count, bool pressed)
{
  char text[12];
  snprintf(text, sizeof(text), "%s:%" PRIu16, label, count);
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_DrawFrame(u8g2, x, 49, 39, 14);
  if (pressed)
  {
    u8g2_DrawBox(u8g2, x + 1, 50, 37, 12);
    u8g2_SetDrawColor(u8g2, 0);
  }
  u8g2_DrawStr(u8g2, x + 3, 59, text);
}

static void frame(prism_t *prism)
{
  if (state == NULL)
  {
    u8g2_t *u8g2 = prism_display(prism);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, 0, 9, "full test");
    u8g2_DrawStr(u8g2, 0, 23, "out of memory");
    return;
  }
  prism_time_t now = prism_now_us(prism);
  ++state->frame_count;
  int64_t elapsed = prism_time_diff_us(
      prism, state->fps_window_started, now);
  if (elapsed >= FPS_WINDOW_US)
  {
    state->fps = (uint32_t)((uint64_t)state->frame_count * 1000000u /
                            (uint64_t)elapsed);
    state->frame_count = 0;
    state->fps_window_started = now;
  }

  state->work_sink = run_workload(state->work_sink ^ (uint32_t)now);

  uint8_t phase = (uint8_t)(prism_millis(prism) * 256u / 4000u);
  prism_color_t left = color_wheel(phase);
  prism_color_t right = color_wheel((uint8_t)(phase + 64u));
  if (prism_button_pressed(prism, PRISM_BUTTON_LEFT))
    left = prism_rgba(255, 255, 255, 255);
  if (prism_button_pressed(prism, PRISM_BUTTON_RIGHT))
    right = prism_rgba(255, 255, 255, 255);
  if (prism_button_pressed(prism, PRISM_BUTTON_MENU))
    left = right = prism_rgba(255, 0, 255, 255);
  prism_led_set(prism, PRISM_LED_LEFT, left);
  prism_led_set(prism, PRISM_LED_RIGHT, right);

  u8g2_t *u8g2 = prism_display(prism);
  char line[32];
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  u8g2_DrawStr(u8g2, 0, 9, "full test");
  snprintf(line, sizeof(line), "fps: %" PRIu32, state->fps);
  u8g2_DrawStr(u8g2, 0, 23, line);
  snprintf(line, sizeof(line), "load: %" PRIu32,
           (uint32_t)WORKLOAD_ITERATIONS);
  u8g2_DrawStr(u8g2, 0, 35, line);
  snprintf(line, sizeof(line), "check: %04" PRIx32,
           state->work_sink & 0xffffu);
  u8g2_DrawStr(u8g2, 0, 46, line);

  draw_button(u8g2, 0, "L", state->button_count[0],
              prism_button_pressed(prism, PRISM_BUTTON_LEFT));
  draw_button(u8g2, 44, "M", state->button_count[2],
              prism_button_pressed(prism, PRISM_BUTTON_MENU));
  draw_button(u8g2, 88, "R", state->button_count[1],
              prism_button_pressed(prism, PRISM_BUTTON_RIGHT));
}

static void leave(prism_t *prism)
{
  if (state == NULL)
    return;
  synth_note(prism, 0, state->auto_note, false);
  static const uint8_t button_notes[] = {48, 60, 72};
  for (uint8_t i = 0; i < 3; ++i)
    synth_note(prism, 1, button_notes[i], false);
  free(state);
  state = NULL;
}

PRISM_CARTRIDGE(cartridge_full_test,
    .id = "dev.preyneyv.prism.full-test",
    .name = "full test",
    .version = 1,
    .tick_divider = 1,
    .icon = diagnostic_icon,
    .enter = enter,
    .tick = tick,
    .frame = frame,
    .leave = leave,
);

#if defined(PRISM_TESTING)
void prism_full_test_set_allocation_failure(bool fail)
{
  test_allocation_failure = fail;
}

bool prism_full_test_has_allocation(void) { return state != NULL; }
#endif
