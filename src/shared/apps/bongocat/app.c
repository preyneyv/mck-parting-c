#include "assets.h"
#include <shared/apps/apps.h>
#include <shared/engine.h>

static void enter() {
  audio_synth_operator_config_t config = audio_synth_operator_config_default;
  config.env = (audio_synth_env_config_t){
      .a = 0,
      .d = 700,
      .s = q1x31_f(.2f), // sustain level
      .r = 200,
  };
  config.freq_mult = 11;
  config.level = q1x15_f(0.3f);
  audio_synth_operator_set_config(&g_engine.synth.voices[0].ops[0], config);
  audio_synth_operator_set_config(&g_engine.synth.voices[1].ops[0], config);

  config = audio_synth_operator_config_default;
  config.env = (audio_synth_env_config_t){
      .a = 0,
      .d = 1200,
      .s = q1x31_f(0.f), // sustain level
      .r = 300,
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
                                      .note_number = note("C4"),
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
                                      .note_number = note("G4"),
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

app_t app_bongocat = {
    .name = "bongocat",
    .icon = icon__0_bits,
    .enter = enter,
    .tick = tick,
};
