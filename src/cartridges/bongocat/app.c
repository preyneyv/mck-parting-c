#include "sprites/cat.h"
#include "sprites/icon.h"

#include <prism/sdk.h>

static void enter(prism_t *prism)
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
  audio_synth_patch_config_set(prism_synth(prism), 0, patch);
}

static void tick(prism_t *prism)
{
  static const uint8_t notes[] = {50, 57};
  for (prism_button_t button = PRISM_BUTTON_LEFT;
       button <= PRISM_BUTTON_RIGHT; ++button)
  {
    uint8_t note_number = notes[button - PRISM_BUTTON_LEFT];
    if (prism_button_keydown(prism, button))
    {
      audio_synth_enqueue(prism_synth(prism),
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
                              .data.note_on =
                                  {
                                      .patch_idx = 0,
                                      .note_number = note_number,
                                      .velocity = 100,
                                  },
                          });
    }
    if (prism_button_keyup(prism, button))
    {
      audio_synth_enqueue(prism_synth(prism),
                          &(audio_synth_message_t){
                              .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
                              .data.note_off = {
                                  .patch_idx = 0,
                                  .note_number = note_number,
                              },
                          });
    }
  }
}

static void frame(prism_t *prism)
{
  u8g2_t *u8g2 = prism_display(prism);
  u8g2_SetDrawColor(u8g2, 1);
  bool left = prism_button_pressed(prism, PRISM_BUTTON_LEFT);
  bool right = prism_button_pressed(prism, PRISM_BUTTON_RIGHT);
  bool breathe = (prism_millis(prism) / 500) % 2;

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

PRISM_CARTRIDGE(cartridge_bongocat,
    .id = "dev.preyneyv.prism.bongocat",
    .name = "bongocat",
    .version = 1,
    .tick_divider = 4,
    .icon = icon__0_bits,
    .enter = enter,
    .tick = tick,
    .frame = frame,
);
