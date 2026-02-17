// todo: pause / resume for timer

#include <ctype.h>
#include <shared/apps/apps.h>
#include <shared/utils/elm.h>
#include <shared/engine.h>
#include <shared/leaderboard/leaderboard.h>
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
  INITIAL_T = 100,
  DURATION_MS = 30000, // 30 seconds
};

typedef struct
{
  uint32_t T; // base time unit in ticks (adaptive)
  uint32_t words[WORD_LOOKAHEAD];
  char target_morse[TARGET_MORSE_COUNT][TARGET_MORSE_LENGTH];
  const char *current_word;
  uint32_t current_letter;
  uint32_t last_edge_tick;
  char input_buffer[TARGET_MORSE_LENGTH * 2];
  uint32_t input_len;
  char input_draft;

  bool started;
  bool finished;
  platform_time_t started_at;
  struct
  {
    uint32_t letters;
    uint32_t errors;
  } stats;

  leaderboard_qrcode_t qr_code;
} state_t;

typedef struct
{
  uint32_t remaining_s;

  uint32_t letters;
  uint32_t errors;
  uint32_t cpm;
  uint32_t accuracy;
} stats_t;

static state_t *state;

static stats_t get_stats()
{
  uint32_t elapsed_ms = (uint32_t)(platform_time_diff_us(state->started_at, platform_now_us()) / 1000);
  if (state->finished)
    elapsed_ms = DURATION_MS;

  uint32_t remaining_ms = DURATION_MS - elapsed_ms;
  uint32_t remaining_s = remaining_ms / 1000;

  uint32_t letters = state->stats.letters;
  uint32_t errors = state->stats.errors;

  uint32_t cpm = (letters * 60000) / (elapsed_ms);
  uint32_t accuracy = (letters + errors) > 0
                          ? (letters * 100) / (letters + errors)
                          : 100;

  if (!state->started)
  {
    cpm = 0;
    accuracy = 100;
    remaining_s = DURATION_MS / 1000;
  }

  return (stats_t){
      .remaining_s = remaining_s,
      .letters = letters,
      .errors = errors,
      .cpm = cpm,
      .accuracy = accuracy,
  };
}

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
  state->input_len = 0;
  state->input_buffer[0] = '\0';
  _generate_morse_for_current_word();
}

static void _finish_game();

static void _start_game()
{
  if (state != NULL)
    free(state);

  state = calloc(1, sizeof(state_t));
  state->T = INITIAL_T;

  state->last_edge_tick = g_engine.tick;
  state->input_len = 0;

  // pick words
  for (uint32_t i = 0; i < WORD_LOOKAHEAD - 1; i++)
  {
    state->words[i] = rand() % WORDS_COUNT;
  }
  // prep first word
  _update_current_word();
}

static void enter()
{
  // setup audio synth
  audio_synth_patch_config_t patch = audio_synth_patch_config_default;
  patch.ops[0].level = q1x15_f(.5f);
  patch.ops[0].env = (audio_synth_env_config_t){
      .a = 2,
      .d = 0,
      .s = q1x31_f(1.0f),
      .r = 5,
  };
  audio_synth_patch_config_set(&g_engine.synth, 0, patch);

  _start_game();
}

static void tick_game()
{
  uint32_t dt = g_engine.tick - state->last_edge_tick;
  if (BUTTON_KEYDOWN(BUTTON_LEFT))
  {
    _start_game();
  }

  if (BUTTON_KEYDOWN(BUTTON_RIGHT))
  {
    if (!state->started)
    {
      state->started = true;
      state->finished = false;
      state->started_at = platform_now_us();
    }

    // begin mark (end gap)
    state->last_edge_tick = g_engine.tick;

    if (dt < 2 * state->T)
    {
      // gap was < 2T, treat as 1T gap
      state->T = (state->T * 9 + dt) / 10;
    }

    audio_synth_enqueue(
        &g_engine.synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_ON,
            .data.note_on =
                {
                    .patch_idx = 0,
                    .note_number = note("C5"),
                    .velocity = 100,
                },
        });
  }
  else if (BUTTON_KEYUP(BUTTON_RIGHT))
  {
    // end mark (begin gap)
    uint32_t dt = g_engine.tick - state->last_edge_tick;
    state->last_edge_tick = g_engine.tick;

    char symbol = 0;
    if (dt > 2 * state->T)
    {
      // mark > 2T -> dash
      if (dt < 5)
      {
        state->T = (state->T * 9 + (dt / 3)) / 10;
      }
      symbol = '-';
    }
    else
    {
      // mark < 2T -> dot
      state->T = (state->T * 9 + dt) / 10;
      symbol = '.';
    }

    if (state->input_len < sizeof(state->input_buffer) - 1)
    {
      state->input_buffer[state->input_len++] = symbol;
      state->input_buffer[state->input_len] = '\0';
    }

    // check current letter match
    const char *target = state->target_morse[state->current_letter];
    size_t target_len = strlen(target);

    // check prefix
    bool match = true;
    for (size_t i = 0; i < state->input_len; i++)
    {
      if (state->input_buffer[i] != target[i])
      {
        match = false;
        break;
      }
    }

    if (!match)
    {
      // mismatch - clear input
      state->input_len = 0;
      state->input_buffer[0] = '\0';
      // ideally play error sound
      state->stats.errors++;
    }
    else if (state->input_len == target_len)
    {
      // letter matched
      state->stats.letters++;

      state->current_letter++;
      state->input_len = 0;
      state->input_buffer[0] = '\0';
      if (state->current_letter >= strlen(state->current_word))
      {
        _update_current_word();
      }
    }

    audio_synth_enqueue(
        &g_engine.synth,
        &(audio_synth_message_t){
            .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
            .data.note_off =
                {
                    .patch_idx = 0,
                    .note_number = note("C5"),
                },
        });
  }
}

static void tick_results()
{
}

static void _finish_game()
{
  engine_buttons_reset();

  state->finished = true;
  audio_synth_enqueue(
      &g_engine.synth,
      &(audio_synth_message_t){
          .type = AUDIO_SYNTH_MESSAGE_NOTE_OFF,
          .data.note_off = {
              .patch_idx = 0,
              .note_number = note("C5"),
          },
      });

  uint8_t stats[8];
  stats[0] = (uint8_t)(state->stats.letters & 0xFF);
  stats[1] = (uint8_t)((state->stats.letters >> 8) & 0xFF);
  stats[2] = (uint8_t)((state->stats.letters >> 16) & 0xFF);
  stats[3] = (uint8_t)((state->stats.letters >> 24) & 0xFF);

  stats[4] = (uint8_t)(state->stats.errors & 0xFF);
  stats[5] = (uint8_t)((state->stats.errors >> 8) & 0xFF);
  stats[6] = (uint8_t)((state->stats.errors >> 16) & 0xFF);
  stats[7] = (uint8_t)((state->stats.errors >> 24) & 0xFF);

  leaderboard_get_qrcode(
      1,
      &stats,
      sizeof(stats), state->qr_code);
}

static void tick()
{
  if (state->finished)
  {
    tick_results();
  }
  else
  {
    tick_game();

    // check for time up
    uint32_t elapsed_ms = (uint32_t)(platform_time_diff_us(state->started_at, platform_now_us()) / 1000);
    if (state->started && elapsed_ms >= DURATION_MS)
    {
      _finish_game();
    }
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
  for (uint32_t i = state->current_letter; i < TARGET_MORSE_COUNT; i++)
  {
    char *morse = state->target_morse[i];
    uint32_t letter_len = strlen(morse);
    if (letter_len == 0 && i > state->current_letter)
      break;

    for (uint32_t j = 0; j < letter_len; j++)
    {
      char symbol = morse[j];
      bool filled = false;
      if (i == state->current_letter && j < state->input_len)
      {
        filled = true;
      }

      if (symbol == '.')
      {
        if (filled)
          elm_disc(root, vec2(x_offset + 2, 2), 2, U8G2_DRAW_ALL);
        else
          elm_circle(root, vec2(x_offset + 2, 2), 2, U8G2_DRAW_ALL);
        x_offset += 4;
      }
      else if (symbol == '-')
      {
        if (filled)
          elm_rounded_box(root, vec2(x_offset, 0), 12, 5, 1);
        else
          elm_rounded_frame(root, vec2(x_offset, 0), 12, 5, 1);
        x_offset += 12;
      }
      x_offset += 4;
    }
    // space between letters
    x_offset += 6;
  }
}

static void _frame_current_input(u8g2_t *u8g2, elm_t *root)
{
  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);

  elm_t ctx = elm_child(root, vec2(33, 0));

  uint32_t dt = g_engine.tick - state->last_edge_tick;
  bool pressed = BUTTON_PRESSED(BUTTON_RIGHT);

  char draft = dt > 2 * state->T ? '-' : '.';
  // elm_frame(&ctx, vec2(0, 4), 61, 4);
  elm_hline(&ctx, vec2(0, 6), 61);
  if (pressed)
  {
    uint32_t width = (dt * 61) / (3 * state->T);
    if (width > 61)
      width = 61;
    elm_box(&ctx, vec2(0, 5), width, 3);
  }

  elm_triangle(&ctx, vec2(28, 2), vec2(30, 5), vec2(33, 2));

  if (pressed && draft == '.')
    elm_disc(&ctx, vec2(-10, 6), 2, U8G2_DRAW_ALL);
  else
    elm_circle(&ctx, vec2(-10, 6), 2, U8G2_DRAW_ALL);

  if (pressed && draft == '-')
    elm_rounded_box(&ctx, vec2(61 + 4, 4), 12, 5, 1);
  else
    elm_rounded_frame(&ctx, vec2(61 + 4, 4), 12, 5, 1);

  elm_str(root, vec2(0, 9), "dit");
  elm_str(root, vec2(128 - 15, 9), "dah");
}

static void _frame_stats(u8g2_t *u8g2, elm_t *root)
{
  stats_t stats = get_stats();

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_7x14_mr);

  char stats_buffer[64];

  elm_t ctx = elm_child(root, vec2(0, 0));
  snprintf(stats_buffer, sizeof(stats_buffer), "%d", stats.cpm / 5);
  elm_str(&ctx, vec2(0, 0), stats_buffer);

  snprintf(stats_buffer, sizeof(stats_buffer), "%d", stats.errors);
  elm_str(&ctx, vec2(28 + 5, 0), stats_buffer);

  snprintf(stats_buffer, sizeof(stats_buffer), "%3d%%", stats.accuracy);
  elm_str(&ctx, vec2(100 - 28 - 8, 0), stats_buffer);

  snprintf(stats_buffer, sizeof(stats_buffer), "%3ds", stats.remaining_s);
  elm_str(&ctx, vec2(100, 0), stats_buffer);

  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  ctx = elm_child(root, vec2(0, 8));
  elm_str(&ctx, vec2(0, 0), "WPM");
  elm_str(&ctx, vec2(28 + 5, 0), "ERR");
  elm_str(&ctx, vec2(100 - 8 - 15, 0), "ACC");
  elm_str(&ctx, vec2(128 - 20, 0), "TIME");
}

static void frame_game()
{
  u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);
  elm_t root = elm_root(u8g2, VEC2_Z);

  elm_t ctx = elm_child(&root, vec2(0, 0));
  _frame_words(u8g2, &ctx);

  ctx = elm_child(&root, vec2(0, 20));
  _frame_target_morse(u8g2, &ctx);

  elm_hline(&root, vec2(0, 27), 128);

  ctx = elm_child(&root, vec2(0, 29));
  _frame_current_input(u8g2, &ctx);

  elm_hline(&root, vec2(0, 43), 128);

  ctx = elm_child(&root, vec2(0, 64 - 8));
  _frame_stats(u8g2, &ctx);
}

static void elm_score(elm_t *parent, vec2_t pos, const char *label, const char *value)
{
  u8g2_t *u8g2 = parent->u8g2;
  elm_t child = elm_child(parent, pos);

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  elm_str(&child, vec2(0, 6), label);
  u8g2_SetFont(u8g2, u8g2_font_7x14_mr);
  elm_str(&child, vec2(0, 6 + 14), value);
}

static void frame_results()
{
  u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);
  elm_t root = elm_root(u8g2, VEC2_Z);

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_7x14_mr);

  char results_buffer[64];
  stats_t stats = get_stats();

  char stat_buffer[16];
  snprintf(stat_buffer, sizeof(stat_buffer), "%d", stats.cpm / 5);
  elm_score(&root, vec2(0, 0), "WPM", stat_buffer);

  snprintf(stat_buffer, sizeof(stat_buffer), "%d", stats.cpm);
  elm_score(&root, vec2(40, 0), "CPM", stat_buffer);

  snprintf(stat_buffer, sizeof(stat_buffer), "%d", stats.errors);
  elm_score(&root, vec2(0, 27), "ERR", stat_buffer);

  snprintf(stat_buffer, sizeof(stat_buffer), "%d%%", stats.accuracy);
  elm_score(&root, vec2(40, 27), "ACC", stat_buffer);

  // draw QR code
  elm_qrcode(&root, vec2(128 - 5, 5), ELM_ALIGN_TOP_RIGHT, state->qr_code, 2, 1);

  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  bool pressed;
  elm_btn(&root, vec2(64, 64), "NEW GAME?", ELM_ALIGN_BOTTOM_CENTER, &pressed);
  if (pressed)
  {
    engine_buttons_reset();
    _start_game();
  }
}

static void frame()
{
  if (state->finished)
  {
    frame_results();
  }
  else
  {
    frame_game();
  }

  leds_set_all(&g_engine.leds, (color_t){.hex = 0x00ff00});
}

static void leave()
{
  if (state != NULL)
    free(state);
  state = NULL;
}

app_t app_morse = {
    .name = "morse",
    .enter = enter,
    .tick = tick,
    .frame = frame,
    .leave = leave,
};
