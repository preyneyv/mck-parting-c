#include <string.h>

#include <prism/sdk.h>

#include "sprites/icon.h"

enum
{
  GUIDE_CARD_TAP,
  GUIDE_CARD_HOLD,
  GUIDE_CARD_MENU,
  GUIDE_CARD_FINISH,
  GUIDE_CARD_COUNT,
  GUIDE_PERSIST_MAGIC = 0x47554944u,
  GUIDE_TRANSITION_MS = 180,
  GUIDE_SUCCESS_DELAY_MS = 300,
  GUIDE_NICE_MS = 700,
  GUIDE_BURST_MS = 560,
  GUIDE_CONTENT_Y = 2,
  GUIDE_LESSON_TAP = 1u << 0,
  GUIDE_LESSON_HOLD = 1u << 1,
  GUIDE_LESSON_MENU = 1u << 2,
  GUIDE_LESSONS_VALID = GUIDE_LESSON_TAP | GUIDE_LESSON_HOLD |
                        GUIDE_LESSON_MENU,
};

typedef struct
{
  uint32_t magic;
  uint8_t card;
  uint8_t finished;
  uint8_t lessons;
  uint8_t reserved;
} guide_persistence_t;

static prism_t *app_prism;
static guide_persistence_t *saved;

static struct
{
  uint8_t card;
  uint8_t previous_card;
  uint8_t lessons;
  uint8_t success_card;
  int8_t direction;
  int8_t pending_direction;
  int8_t success_x;
  int8_t success_y;
  uint8_t success_radius_x;
  uint8_t success_radius_y;
  int32_t slide;
  int32_t nice_y;
  int32_t left_outline;
  int32_t right_outline;
  bool left_pressed;
  bool right_pressed;
  bool ignore_left_release;
  bool ignore_right_release;
  bool hold_latched;
  bool menu_opened;
  bool delayed_navigation;
  bool closing;
  uint32_t success_started;
  prism_ui_hold_feedback_t hold_feedback;
} guide;

static uint16_t text_width(u8g2_t *u8g2, const char *text)
{
  return u8g2_GetStrWidth(u8g2, text);
}

static void draw_centered(u8g2_t *u8g2, int16_t origin_x, int16_t y,
                          const char *text)
{
  u8g2_DrawStr(u8g2, origin_x + (DISP_WIDTH - text_width(u8g2, text)) / 2,
               y, text);
}

static bool lesson_complete(uint8_t lesson)
{
  return (guide.lessons & lesson) != 0;
}

static uint8_t lesson_for_card(uint8_t card)
{
  switch (card)
  {
  case GUIDE_CARD_TAP:
    return GUIDE_LESSON_TAP;
  case GUIDE_CARD_HOLD:
    return GUIDE_LESSON_HOLD;
  case GUIDE_CARD_MENU:
    return GUIDE_LESSON_MENU;
  default:
    return 0;
  }
}

static void save_progress(void)
{
  if (saved == NULL)
    return;
  saved->magic = GUIDE_PERSIST_MAGIC;
  saved->card = guide.card;
  saved->finished = 0;
  saved->lessons = guide.lessons;
  prism_persist(app_prism);
}

static void draw_progress(u8g2_t *u8g2)
{
  enum
  {
    ACTIVE_SIZE = 6,
    INDICATOR_SPACING = 10
  };
  int16_t left =
      (DISP_WIDTH - ((GUIDE_CARD_COUNT - 1) * INDICATOR_SPACING +
                     ACTIVE_SIZE)) /
      2;
  for (uint8_t i = 0; i < GUIDE_CARD_COUNT; ++i)
  {
    int16_t x = left + i * INDICATOR_SPACING;
    int16_t center = x + ACTIVE_SIZE / 2;
    uint8_t lesson = lesson_for_card(i);
    bool complete = lesson != 0 && lesson_complete(lesson);
    if (i == guide.card)
    {
      u8g2_DrawFrame(u8g2, x, 1, ACTIVE_SIZE, ACTIVE_SIZE);
      if (complete)
      {
        u8g2_DrawLine(u8g2, x + 1, 3, x + 2, 5);
        u8g2_DrawLine(u8g2, x + 2, 5, x + ACTIVE_SIZE, 1);
      }
    }
    else if (complete)
      u8g2_DrawBox(u8g2, center - 1, 2, 3, 3);
    else
      u8g2_DrawPixel(u8g2, center, 3);
  }
}

static void draw_sparkle(u8g2_t *u8g2, int16_t x, int16_t y,
                         uint8_t phase)
{
  if ((phase & 1u) == 0)
  {
    u8g2_DrawPixel(u8g2, x, y - 2);
    u8g2_DrawPixel(u8g2, x, y + 2);
    u8g2_DrawPixel(u8g2, x - 2, y);
    u8g2_DrawPixel(u8g2, x + 2, y);
  }
  u8g2_DrawPixel(u8g2, x, y);
}

static void draw_burst_for_card(u8g2_t *u8g2, int16_t card_x,
                                uint8_t card)
{
  if (guide.success_card != card)
    return;
  uint32_t elapsed = prism_ticks(app_prism) - guide.success_started;
  uint32_t duration = prism_ticks_from_ms(GUIDE_BURST_MS);
  if (elapsed > duration)
    return;

  static const int16_t directions[8][2] = {
      {-256, 0},
      {256, 0},
      {0, -256},
      {0, 256},
      {-181, -181},
      {181, -181},
      {-181, 181},
      {181, 181},
  };
  for (uint8_t i = 0; i < 8; ++i)
  {
    /* Let the faster flecks disappear first instead of dropping the whole
     * burst on one frame. */
    uint32_t particle_duration =
        duration - prism_ticks_from_ms((uint32_t)(i & 3u) * 24u);
    if (elapsed >= particle_duration)
      continue;

    int16_t dx = directions[i][0];
    int16_t dy = directions[i][1];
    int16_t edge_x = dx == 0  ? 0
                     : dx < 0 ? -(int16_t)guide.success_radius_x
                              : (int16_t)guide.success_radius_x;
    int16_t edge_y = dy == 0  ? 0
                     : dy < 0 ? -(int16_t)guide.success_radius_y
                              : (int16_t)guide.success_radius_y;
    uint32_t age = elapsed * 256u / duration;
    uint32_t travel = age * (8u + (i * 3u) % 5u) / 256u;
    int16_t gravity = (int16_t)(age * age * 7u / (256u * 256u));
    int16_t x = card_x + guide.success_x + edge_x +
                (int16_t)(dx * (int32_t)travel / 256);
    int16_t y = guide.success_y + edge_y +
                (int16_t)(dy * (int32_t)travel / 256) + gravity;
    if (x >= 0 && x < DISP_WIDTH && y >= 0 && y < DISP_HEIGHT)
      u8g2_DrawPixel(u8g2, x, y);
  }
}

static void draw_status(u8g2_t *u8g2, int16_t card_x, uint8_t card)
{
  if (guide.success_card != card ||
      prism_ticks(app_prism) - guide.success_started >=
          prism_ticks_from_ms(GUIDE_NICE_MS))
    return;
  u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
  const char *label = "nice!";
  draw_centered(u8g2, card_x, (int16_t)guide.nice_y, label);
}

static void draw_badge(u8g2_t *u8g2, int16_t x, int16_t y, uint16_t size,
                       const char *label, int32_t outline, float ratio,
                       bool from_right)
{
  elm_t root = elm_root(u8g2, VEC2_Z);
  elm_input_badge(&root, vec2(x, y), size, label, outline, ratio,
                  from_right);
}

static void draw_card_tap(u8g2_t *u8g2, int16_t x)
{
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  draw_centered(u8g2, x, 15 + GUIDE_CONTENT_Y, "tap to move");
  draw_badge(u8g2, x + 9, 23 + GUIDE_CONTENT_Y, 24, "L",
             guide.left_outline, 0.f, false);
  draw_badge(u8g2, x + 95, 23 + GUIDE_CONTENT_Y, 24, "R",
             guide.right_outline, 0.f, true);
  u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
  draw_centered(u8g2, x, 38 + GUIDE_CONTENT_Y, "back  /  next");
  draw_status(u8g2, x, GUIDE_CARD_TAP);
  draw_burst_for_card(u8g2, x, GUIDE_CARD_TAP);
}

static void draw_card_hold(u8g2_t *u8g2, int16_t x)
{
  float left = prism_button_hold_ratio(app_prism, PRISM_BUTTON_LEFT);
  float right = prism_button_hold_ratio(app_prism, PRISM_BUTTON_RIGHT);
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  draw_centered(u8g2, x, 15 + GUIDE_CONTENT_Y, "hold to choose");
  draw_badge(u8g2, x + 28, 22 + GUIDE_CONTENT_Y, 26, "L",
             guide.left_outline, left, false);
  draw_badge(u8g2, x + 74, 22 + GUIDE_CONTENT_Y, 26, "R",
             guide.right_outline, right, true);
  draw_status(u8g2, x, GUIDE_CARD_HOLD);
  draw_burst_for_card(u8g2, x, GUIDE_CARD_HOLD);
}

static void draw_card_menu(u8g2_t *u8g2, int16_t x)
{
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  u8g2_DrawStr(u8g2, x + 8, 15 + GUIDE_CONTENT_Y, "there's a");
  u8g2_DrawStr(u8g2, x + 8, 27 + GUIDE_CONTENT_Y, "menu button");

  u8g2_DrawLine(u8g2, x + 120, 18 + GUIDE_CONTENT_Y, x + 120, 3);
  u8g2_DrawLine(u8g2, x + 120, 3, x + 115, 8);
  u8g2_DrawLine(u8g2, x + 120, 3, x + 125, 8);

  u8g2_SetFont(u8g2, u8g2_font_4x6_tf);
  const char *line_1 = "press to pause, sleep,";
  const char *line_2 = "or go home";
  u8g2_DrawStr(u8g2, x + 120 - text_width(u8g2, line_1),
               39 + GUIDE_CONTENT_Y, line_1);
  u8g2_DrawStr(u8g2, x + 120 - text_width(u8g2, line_2),
               48 + GUIDE_CONTENT_Y, line_2);
  draw_status(u8g2, x, GUIDE_CARD_MENU);
  draw_burst_for_card(u8g2, x, GUIDE_CARD_MENU);
}

static void draw_finish_button(u8g2_t *u8g2, int16_t x, float left,
                               float right)
{
  float ratio = left;
  bool from_right = false;
  if (right >= left)
  {
    ratio = right;
    from_right = true;
  }

  elm_t root = elm_root(u8g2, vec2(x, 0));
  elm_btn_input(&root, vec2(DISP_WIDTH / 2, 48 + GUIDE_CONTENT_Y), 68,
                "hold to play",
                ELM_ALIGN_TOP_CENTER, ratio, from_right, NULL);
}

static void draw_card_finish(u8g2_t *u8g2, int16_t x)
{
  u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
  draw_centered(u8g2, x, 15 + GUIDE_CONTENT_Y, "you're ready!");
  u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
  draw_centered(u8g2, x, 30 + GUIDE_CONTENT_Y, "more at");
  draw_centered(u8g2, x, 41 + GUIDE_CONTENT_Y, "prism.preyneyv.dev");
  draw_finish_button(
      u8g2, x, prism_button_hold_ratio(app_prism, PRISM_BUTTON_LEFT),
      prism_button_hold_ratio(app_prism, PRISM_BUTTON_RIGHT));
  draw_sparkle(u8g2, x + 18, 16 + GUIDE_CONTENT_Y,
               (uint8_t)(prism_millis(app_prism) / 300u));
  draw_sparkle(u8g2, x + 111, 20 + GUIDE_CONTENT_Y,
               (uint8_t)(prism_millis(app_prism) / 420u + 1u));
}

static void draw_card(u8g2_t *u8g2, uint8_t card, int16_t x)
{
  switch (card)
  {
  case GUIDE_CARD_TAP:
    draw_card_tap(u8g2, x);
    break;
  case GUIDE_CARD_HOLD:
    draw_card_hold(u8g2, x);
    break;
  case GUIDE_CARD_MENU:
    draw_card_menu(u8g2, x);
    break;
  case GUIDE_CARD_FINISH:
    draw_card_finish(u8g2, x);
    break;
  default:
    break;
  }
}

static void navigate(int8_t direction)
{
  uint8_t lesson = lesson_for_card(guide.card);
  if (direction > 0 && lesson != 0 && !lesson_complete(lesson))
    return;

  int16_t next = (int16_t)guide.card + direction;
  if (next < 0 || next >= GUIDE_CARD_COUNT || guide.slide != 0)
    return;

  guide.previous_card = guide.card;
  guide.card = (uint8_t)next;
  guide.direction = direction;
  guide.slide = direction > 0 ? DISP_WIDTH : -DISP_WIDTH;
  guide.hold_latched = false;
  prism_ui_hold_feedback_reset(&guide.hold_feedback);
  prism_anim_to(app_prism, &guide.slide, 0, GUIDE_TRANSITION_MS,
                PRISM_ANIM_EASE_INOUT_QUAD, NULL, NULL);
  prism_ui_navigate(app_prism);
  save_progress();
}

static void start_burst(uint8_t card, int8_t x, int8_t y,
                        uint8_t radius_x, uint8_t radius_y)
{
  guide.success_card = card;
  guide.success_x = x;
  guide.success_y = y;
  guide.success_radius_x = radius_x;
  guide.success_radius_y = radius_y;
  guide.success_started = prism_ticks(app_prism);
  guide.nice_y = DISP_HEIGHT + 3;
  prism_anim_cancel(app_prism, &guide.nice_y, false);
  prism_anim_to(app_prism, &guide.nice_y, 61, 180,
                PRISM_ANIM_EASE_OUT_CUBIC, NULL, NULL);
}

static void begin_lesson_success(uint8_t lesson, uint8_t card,
                                 int8_t x, int8_t y,
                                 uint8_t radius_x, uint8_t radius_y,
                                 bool navigate_after_delay)
{
  bool first = !lesson_complete(lesson);
  start_burst(card, x, y, radius_x, radius_y);
  if (first)
  {
    guide.lessons |= lesson;
    save_progress();
  }
  guide.delayed_navigation = navigate_after_delay;
  if (navigate_after_delay)
    guide.pending_direction = 1;
}

static void update_outline(prism_t *prism, bool pressed, bool *previous,
                           volatile int32_t *outline)
{
  if (pressed == *previous)
    return;
  *previous = pressed;
  prism_anim_cancel(prism, outline, false);
  prism_anim_to(prism, outline,
                pressed ? ELM_INPUT_BADGE_OUTLINE_SCALE : 0,
                pressed ? 110u : 180u, PRISM_ANIM_EASE_OUT_CUBIC,
                NULL, NULL);
}

static void begin_close(void)
{
  if (guide.closing)
    return;
  guide.closing = true;
  if (saved != NULL)
  {
    saved->magic = GUIDE_PERSIST_MAGIC;
    saved->card = 0;
    saved->finished = 1;
    saved->lessons = guide.lessons;
    prism_persist(app_prism);
  }
  prism_buttons_reset(app_prism);
  prism_close(app_prism);
}

static void enter(prism_t *prism)
{
  app_prism = prism;
  saved = prism->persistent_size >= sizeof(*saved) ? prism->persistent : NULL;
  memset(&guide, 0, sizeof(guide));
  guide.success_card = UINT8_MAX;
  guide.nice_y = 61;

  if (saved != NULL && saved->magic == GUIDE_PERSIST_MAGIC)
  {
    guide.lessons = saved->lessons & GUIDE_LESSONS_VALID;
    if (!saved->finished && saved->card < GUIDE_CARD_COUNT)
      guide.card = saved->card;
    else
    {
      saved->card = 0;
      saved->finished = 0;
      prism_persist(prism);
    }
  }
  else if (saved != NULL)
  {
    memset(saved, 0, sizeof(*saved));
    saved->magic = GUIDE_PERSIST_MAGIC;
    prism_persist(prism);
  }
}

static void clear_release(prism_button_t button)
{
  if (button == PRISM_BUTTON_LEFT)
    guide.ignore_left_release = false;
  else
    guide.ignore_right_release = false;
  prism_ui_hold_feedback_reset(&guide.hold_feedback);
}

static void handle_tap(prism_button_t button)
{
  int8_t direction = button == PRISM_BUTTON_LEFT ? -1 : 1;
  if (guide.card != GUIDE_CARD_TAP)
  {
    navigate(direction);
    return;
  }

  int8_t x = button == PRISM_BUTTON_LEFT ? 21 : 107;
  begin_lesson_success(GUIDE_LESSON_TAP, GUIDE_CARD_TAP,
                       x, 35 + GUIDE_CONTENT_Y, 12, 12, true);
}

static void tick(prism_t *prism)
{
  if (guide.card == GUIDE_CARD_MENU &&
      prism_button_keydown(prism, PRISM_BUTTON_MENU))
    start_burst(GUIDE_CARD_MENU, 120, 4, 0, 0);

  bool left_pressed = prism_button_pressed(prism, PRISM_BUTTON_LEFT);
  bool right_pressed = prism_button_pressed(prism, PRISM_BUTTON_RIGHT);
  update_outline(prism, left_pressed, &guide.left_pressed,
                 &guide.left_outline);
  update_outline(prism, right_pressed, &guide.right_pressed,
                 &guide.right_outline);

  float left_hold = prism_button_hold_ratio(prism, PRISM_BUTTON_LEFT);
  float right_hold = prism_button_hold_ratio(prism, PRISM_BUTTON_RIGHT);
  if (left_hold > 0.f)
    guide.ignore_left_release = true;
  if (right_hold > 0.f)
    guide.ignore_right_release = true;

  if (guide.closing)
    return;

  if (guide.delayed_navigation)
  {
    if (prism_ticks(prism) - guide.success_started >=
        prism_ticks_from_ms(GUIDE_SUCCESS_DELAY_MS))
    {
      guide.delayed_navigation = false;
      navigate(guide.pending_direction);
    }
    if (prism_button_keyup(prism, PRISM_BUTTON_LEFT))
      clear_release(PRISM_BUTTON_LEFT);
    if (prism_button_keyup(prism, PRISM_BUTTON_RIGHT))
      clear_release(PRISM_BUTTON_RIGHT);
    return;
  }

  float active_hold = 0.f;
  if (guide.card == GUIDE_CARD_HOLD || guide.card == GUIDE_CARD_FINISH)
    active_hold = left_hold > right_hold ? left_hold : right_hold;
  prism_ui_hold_feedback_update(prism, &guide.hold_feedback, active_hold);

  if (active_hold == 0.f)
    guide.hold_latched = false;

  if (guide.card == GUIDE_CARD_HOLD && active_hold >= 1.f &&
      !guide.hold_latched)
  {
    guide.hold_latched = true;
    prism_button_t button = left_hold >= right_hold
                                ? PRISM_BUTTON_LEFT
                                : PRISM_BUTTON_RIGHT;
    int8_t x = button == PRISM_BUTTON_LEFT ? 41 : 87;
    begin_lesson_success(GUIDE_LESSON_HOLD, GUIDE_CARD_HOLD,
                         x, 35 + GUIDE_CONTENT_Y, 13, 13, true);
  }
  else if (guide.card == GUIDE_CARD_FINISH && active_hold >= 1.f &&
           !guide.hold_latched)
  {
    guide.hold_latched = true;
    begin_close();
    return;
  }

  if (prism_button_keyup(prism, PRISM_BUTTON_LEFT))
  {
    if (!guide.ignore_left_release)
      handle_tap(PRISM_BUTTON_LEFT);
    clear_release(PRISM_BUTTON_LEFT);
  }
  if (prism_button_keyup(prism, PRISM_BUTTON_RIGHT))
  {
    if (!guide.ignore_right_release)
      handle_tap(PRISM_BUTTON_RIGHT);
    clear_release(PRISM_BUTTON_RIGHT);
  }
}

static void frame(prism_t *prism)
{
  u8g2_t *u8g2 = prism_display(prism);
  u8g2_SetDrawColor(u8g2, 1);

  if (guide.slide == 0)
    draw_card(u8g2, guide.card, 0);
  else if (guide.direction > 0)
  {
    /* The next card enters from the right over an opaque moving boundary.
     * Every coordinate passed to U8g2 remains nonnegative. */
    draw_card(u8g2, guide.previous_card, 0);
    int16_t incoming_x = (int16_t)guide.slide;
    if (incoming_x < DISP_WIDTH)
    {
      u8g2_SetDrawColor(u8g2, 0);
      u8g2_DrawBox(u8g2, incoming_x, 0,
                   DISP_WIDTH - incoming_x, DISP_HEIGHT);
      u8g2_SetDrawColor(u8g2, 1);
      draw_card(u8g2, guide.card, incoming_x);
    }
  }
  else
  {
    /* Moving backward reveals the previous card underneath while the current
     * card slides right, again avoiding negative coordinates. */
    draw_card(u8g2, guide.card, 0);
    int16_t outgoing_x = (int16_t)guide.slide + DISP_WIDTH;
    if (outgoing_x < DISP_WIDTH)
    {
      u8g2_SetDrawColor(u8g2, 0);
      u8g2_DrawBox(u8g2, outgoing_x, 0,
                   DISP_WIDTH - outgoing_x, DISP_HEIGHT);
      u8g2_SetDrawColor(u8g2, 1);
      draw_card(u8g2, guide.previous_card, outgoing_x);
    }
  }
  u8g2_SetDrawColor(u8g2, 1);
  draw_progress(u8g2);

  float left = prism_button_hold_ratio(prism, PRISM_BUTTON_LEFT);
  float right = prism_button_hold_ratio(prism, PRISM_BUTTON_RIGHT);
  uint8_t left_level =
      left > 0.f
          ? (uint8_t)(40.f + 180.f * left)
      : prism_button_pressed(prism, PRISM_BUTTON_LEFT) ? 42u
                                                       : 8u;
  uint8_t right_level =
      right > 0.f
          ? (uint8_t)(40.f + 180.f * right)
      : prism_button_pressed(prism, PRISM_BUTTON_RIGHT) ? 42u
                                                        : 8u;
  prism_led_set(prism, PRISM_LED_LEFT,
                prism_rgba(left_level, left_level, left_level, 255));
  prism_led_set(prism, PRISM_LED_RIGHT,
                prism_rgba(right_level, right_level, right_level, 255));
}

static void pause(prism_t *prism)
{
  (void)prism;
  guide.menu_opened = guide.card == GUIDE_CARD_MENU;
  guide.ignore_left_release = false;
  guide.ignore_right_release = false;
  prism_ui_hold_feedback_reset(&guide.hold_feedback);
}

static void resume(prism_t *prism)
{
  if (guide.menu_opened && guide.card == GUIDE_CARD_MENU)
    begin_lesson_success(GUIDE_LESSON_MENU, GUIDE_CARD_MENU,
                         120, 4, 0, 0, true);
  guide.menu_opened = false;
  prism_buttons_reset(prism);
}

PRISM_CARTRIDGE(cartridge_guide,
                .id = "dev.preyneyv.prism.guide",
                .name = "guide",
                .version = 1,
                .tick_divider = 4,
                .icon = icon__0_bits,
                .enter = enter,
                .tick = tick,
                .frame = frame,
                .pause = pause,
                .resume = resume,
                .persistent_size = sizeof(guide_persistence_t),
                .persistent_schema_version = 2, );
