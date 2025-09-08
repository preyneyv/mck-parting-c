#include "assets.h"
#include <shared/apps/apps.h>
#include <shared/engine.h>

static void enter() {
  audio_synth_operator_config_t config = audio_synth_operator_config_default;
  config.env = (audio_synth_env_config_t){
      .a = 2,
      .d = 50,
      .s = q1x31_f(0.f), // sustain level
      .r = 50,
  };
  config.freq_mult = 6;
  config.level = q1x15_f(.4f);
  audio_synth_operator_set_config(&g_engine.synth.voices[0].ops[0], config);
  audio_synth_operator_set_config(&g_engine.synth.voices[1].ops[0], config);

  config = audio_synth_operator_config_default;
  config.env = (audio_synth_env_config_t){
      .a = 2,
      .d = 150,
      .s = q1x31_f(0.f), // sustain level
      .r = 100,
  };
  config.level = q1x15_f(.5f);
  config.mode = AUDIO_SYNTH_OP_MODE_FREQ_MOD;
  audio_synth_operator_set_config(&g_engine.synth.voices[0].ops[1], config);
  audio_synth_operator_set_config(&g_engine.synth.voices[1].ops[1], config);
}

static void tick() {
  if (g_engine.buttons.left.edge) {
    if (g_engine.buttons.left.pressed) {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                              .data.note_on =
                                  {
                                      .voice = 0,
                                      .note_number = 50,
                                      .velocity = 100,
                                  },
                          });
    } else {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                              .data.note_off = {.voice = 0},
                          });
    }
  }
  if (g_engine.buttons.right.edge) {
    if (g_engine.buttons.right.pressed) {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                              .data.note_on =
                                  {
                                      .voice = 1,
                                      .note_number = 57,
                                      .velocity = 100,
                                  },
                          });
    } else {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                              .data.note_off = {.voice = 1},
                          });
    }
  }
}

static void frame() {
  u8g2_t *u8g2 = &g_engine.display.u8g2;
  u8g2_SetDrawColor(u8g2, 1);
  bool left = BUTTON_PRESSED(BUTTON_LEFT);
  bool right = BUTTON_PRESSED(BUTTON_RIGHT);
  bool breathe = (g_engine.tick / 500) % 2;

  const uint8_t *cat = cat_idle_0_bits;
  if (left && right) {
    cat = cat_both_0_bits;
  } else if (left) {
    cat = cat_left_0_bits;
  } else if (right) {
    cat = cat_right_0_bits;
  } else if (breathe) {
    cat = cat_idle_1_bits;
  }
  u8g2_DrawXBM(u8g2, 0, 0, 128, 64, cat);
}

app_t app_bongocat = {
    .name = "bongocat",
    .icon = icon__0_bits,
    .enter = enter,
    .tick = tick,
    .frame = frame,
};
