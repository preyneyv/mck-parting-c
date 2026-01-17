#include <ctype.h>
#include <shared/apps/apps.h>
#include <shared/utils/elm.h>
#include <shared/engine.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// top 200 english words (199 because i removed "I")
// https://github.com/monkeytypegame/monkeytype/blob/10130d73481ce1277a13845c0b1810aa77d47c11/frontend/static/languages/english.json
static const char *words[] = {
    "the", "be", "of", "and", "a", "to",
    "in", "he", "have", "it", "that", "for",
    "they", "with", "as", "not", "on", "she",
    "at", "by", "this", "we", "you", "do",
    "but", "from", "or", "which", "one", "would",
    "all", "will", "there", "say", "who", "make",
    "when", "can", "more", "if", "no", "man",
    "out", "other", "so", "what", "time", "up",
    "go", "about", "than", "into", "could", "state",
    "only", "new", "year", "some", "take", "come",
    "these", "know", "see", "use", "get", "like",
    "then", "first", "any", "work", "now", "may",
    "such", "give", "over", "think", "most", "even",
    "find", "day", "also", "after", "way", "many",
    "must", "look", "before", "great", "back", "through",
    "long", "where", "much", "should", "well", "people",
    "down", "own", "just", "because", "good", "each",
    "those", "feel", "seem", "how", "high", "too",
    "place", "little", "world", "very", "still", "nation",
    "hand", "old", "life", "tell", "write", "become",
    "here", "show", "house", "both", "between", "need",
    "mean", "call", "develop", "under", "last", "right",
    "move", "thing", "general", "school", "never", "same",
    "another", "begin", "while", "number", "part", "turn",
    "real", "leave", "might", "want", "point", "form",
    "off", "child", "few", "small", "since", "against",
    "ask", "late", "home", "interest", "large", "person",
    "end", "open", "public", "follow", "during", "present",
    "without", "again", "hold", "govern", "around", "possible",
    "head", "consider", "word", "program", "problem", "however",
    "lead", "system", "set", "order", "eye", "plan",
    "run", "keep", "face", "fact", "group", "play",
    "stand", "increase", "early", "course", "change", "help",
    "line"};

#define WORDS_COUNT (sizeof(words) / sizeof(words[0]))

static const char *morse_lookup[26] = {
    ".-",   // A
    "-...", // B
    "-.-.", // C
    "-..",  // D
    ".",    // E
    "..-.", // F
    "--.",  // G
    "....", // H
    "..",   // I
    ".---", // J
    "-.-",  // K
    ".-..", // L
    "--",   // M
    "-.",   // N
    "---",  // O
    ".--.", // P
    "--.-", // Q
    ".-.",  // R
    "...",  // S
    "-",    // T
    "..-",  // U
    "...-", // V
    ".--",  // W
    "-..-", // X
    "-.--", // Y
    "--.."  // Z
};

// adaptive morse code decoding logic
// < 1T = .
// > 3T = -
// > 5T = reset word
// we divide at 2T but show progress bar for 1T - 3T
// we learn T from every transition gap (gated)
enum
{
  WORD_LOOKAHEAD = 5,
  TARGET_MORSE_COUNT = 16,
  TARGET_MORSE_LENGTH = 5,
};

typedef struct
{
  uint32_t T; // base time unit in ticks (adaptive)
  uint32_t words[WORD_LOOKAHEAD];
  char target_morse[TARGET_MORSE_COUNT][TARGET_MORSE_LENGTH];
  const char *current_word;
  uint32_t current_letter;
} state_t;

static state_t *state;

static void _generate_morse_for_current_word()
{
  for (uint32_t letter_index = 0; letter_index < TARGET_MORSE_COUNT; letter_index++)
  {
    state->target_morse[letter_index][0] = '\0';
  }
  const char *word = state->current_word;
  for (uint32_t letter_index = 0; letter_index < strlen(word); letter_index++)
  {
    char c = word[letter_index];
    if (c >= 'a' && c <= 'z')
    {
      strcat(state->target_morse[letter_index], morse_lookup[c - 'a']);
    }
  }
}

static void _update_current_word()
{
  // shift words left
  for (uint32_t i = 0; i < WORD_LOOKAHEAD - 1; i++)
  {
    state->words[i] = state->words[i + 1];
  }
  state->words[WORD_LOOKAHEAD - 1] = rand() % WORDS_COUNT;
  state->current_word = words[state->words[0]];
  state->current_letter = 0;
  _generate_morse_for_current_word();
}

static void enter()
{
  state = calloc(1, sizeof(state_t));

  // pick words
  for (uint32_t i = 0; i < WORD_LOOKAHEAD - 1; i++)
  {
    state->words[i] = rand() % WORDS_COUNT;
  }
  // prep first word
  _update_current_word();

  // setup audio synth
  audio_synth_operator_config_t config = audio_synth_operator_config_default;
  config.level = q1x15_f(.5f);
  config.env = (audio_synth_env_config_t){
      .a = 2,
      .d = 0,
      .s = q1x31_f(1.0f),
      .r = 5,
  };
  audio_synth_operator_set_config(&g_engine.synth.voices[0].ops[0], config);
}

static void tick()
{
  if (BUTTON_KEYDOWN(BUTTON_RIGHT))
  {
    // begin mark
    // update T based on time since last keyup
    audio_synth_enqueue(
        &g_engine.synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
            .data.note_on =
                {
                    .voice = 0,
                    .note_number = note("C5"),
                    .velocity = 100,
                },
        });
  }
  else if (BUTTON_KEYUP(BUTTON_RIGHT))
  {
    // end mark
    // update T based on time since keydown
    audio_synth_enqueue(
        &g_engine.synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
            .data.note_off =
                {
                    .voice = 0,
                },
        });
  }
  else if (BUTTON_KEYDOWN(BUTTON_LEFT))
  {
    // reset current word progress
    // reset T to initial
  }
}

static void _frame_words(u8g2_t *u8g2, elm_t *root)
{
  // draw first word
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_7x14B_mr);
  char lookahead_buffer[128];
  lookahead_buffer[0] = '\0';
  snprintf(lookahead_buffer, sizeof(lookahead_buffer), "%s ", state->current_word);
  elm_t first_word = elm_str(root, vec2(0, 13), lookahead_buffer);
  uint32_t first_word_width = u8g2_GetStrWidth(u8g2, lookahead_buffer);

  // highlight current letter
  elm_box(&first_word,
          vec2(state->current_letter * 7, 3),
          6, 1);

  // draw lookahead
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_7x14_mr);
  lookahead_buffer[0] = '\0';
  for (uint32_t i = 1; i < WORD_LOOKAHEAD; i++)
  {
    snprintf(lookahead_buffer + strlen(lookahead_buffer),
             sizeof(lookahead_buffer) - strlen(lookahead_buffer), "%s ",
             words[state->words[i]]);
  }
  elm_str(&first_word, vec2(first_word_width, 0), lookahead_buffer);
}

static void _frame_target_morse(u8g2_t *u8g2, elm_t *root)
{

  u8g2_SetDrawColor(u8g2, 1);

  uint32_t x_offset = 0;
  bool is_first = true;
  for (uint32_t i = state->current_letter; i < TARGET_MORSE_COUNT; i++)
  {
    char *morse = state->target_morse[i];
    uint32_t letter_len = strlen(morse);
    for (uint32_t j = 0; j < letter_len; j++)
    {
      char symbol = morse[j];
      if (symbol == '.')
      {
        if (is_first)
          elm_disc(root, vec2(x_offset + 2, 2), 2, U8G2_DRAW_ALL);
        else
          elm_circle(root, vec2(x_offset + 2, 2), 2, U8G2_DRAW_ALL);
        x_offset += 4;
      }
      else if (symbol == '-')
      {
        if (is_first)
          elm_rounded_box(root, vec2(x_offset, 0), 12, 5, 1);
        else
          elm_rounded_frame(root, vec2(x_offset, 0), 12, 5, 1);
        x_offset += 12;
      }
      x_offset += 4;
    }
    // space between letters
    x_offset += 6;
    is_first = false;
  }
}

static void frame()
{
  u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);
  elm_t root = elm_root(u8g2, VEC2_Z);

  elm_t ctx = elm_child(&root, vec2(0, 0));
  _frame_words(u8g2, &ctx);

  ctx = elm_child(&root, vec2(0, 20));
  _frame_target_morse(u8g2, &ctx);

  leds_set_all(&g_engine.leds, (color_t){.hex = 0x00ff00});
}

static void leave()
{
  free(state);
}

app_t app_morse = {
    .name = "morse",
    .enter = enter,
    .tick = tick,
    .frame = frame,
    .leave = leave,
};
