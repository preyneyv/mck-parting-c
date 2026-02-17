#include "sprites/cat.h"
#include "sprites/icon.h"

#include <shared/apps/apps.h>
#include <shared/engine.h>

static void enter()
{
  audio_synth_patch_config_t patch =
      audio_synth_patch_config_default;
  patch.ops[0].env = (audio_synth_env_config_t){
      .a = 2,
      .d = 50,
      .s = q1x31_f(0.f), // sustain level
      .r = 50,
  };
  patch.ops[0].freq_mult = 6;
  patch.ops[0].level = q1x15_f(.4f);

  patch.ops[1].env = (audio_synth_env_config_t){
      .a = 2,
      .d = 150,
      .s = q1x31_f(0.f), // sustain level
      .r = 100,
  };
  patch.ops[1].level = q1x15_f(.5f);
  patch.ops[1].mode = AUDIO_SYNTH_OP_MODE_FREQ_MOD;
  audio_synth_patch_config_set(&g_engine.synth, 0, patch);
}

static void tick()
{
  if (g_engine.buttons.left.edge)
  {
    if (g_engine.buttons.left.pressed)
    {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                              .data.note_on =
                                  {
                                      .patch_idx = 0,
                                      .note_number = 50,
                                      .velocity = 100,
                                  },
                          });
    }
    else
    {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                              .data.note_off = {.patch_idx = 0, .note_number = 50},
                          });
    }
  }
  if (g_engine.buttons.right.edge)
  {
    if (g_engine.buttons.right.pressed)
    {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                              .data.note_on =
                                  {
                                      .patch_idx = 0,
                                      .note_number = 57,
                                      .velocity = 100,
                                  },
                          });
    }
    else
    {
      audio_synth_enqueue(&g_engine.synth,
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                              .data.note_off = {.patch_idx = 0, .note_number = 57},
                          });
    }
  }
}

static void frame()
{
  u8g2_t *u8g2 = &g_engine.display.u8g2;
  u8g2_SetDrawColor(u8g2, 1);
  bool left = BUTTON_PRESSED(BUTTON_LEFT);
  bool right = BUTTON_PRESSED(BUTTON_RIGHT);
  bool breathe = (g_engine.tick / 500) % 2;

  const uint8_t *cat = cat_idle_0_bits;
  if (left && right)
  {
    cat = cat_both_0_bits;
  }
  else if (left)
  {
    cat = cat_left_0_bits;
  }
  else if (right)
  {
    cat = cat_right_0_bits;
  }
  else if (breathe)
  {
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
