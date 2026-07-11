#pragma once

/* Temporary source-migration helpers for the bundled cartridges. New
 * cartridges should use prism/sdk.h directly. Each translation unit defines
 * PRISM_CONTEXT before including this file. */
#include <prism/sdk.h>

#define BUTTON_NONE PRISM_BUTTON_NONE
#define BUTTON_LEFT PRISM_BUTTON_LEFT
#define BUTTON_RIGHT PRISM_BUTTON_RIGHT
#define BUTTON_MENU PRISM_BUTTON_MENU
typedef prism_button_t button_id_t;
#define BUTTON_PRESSED(button) prism_button_pressed(PRISM_CONTEXT, (button))
#define BUTTON_KEYDOWN(button) prism_button_keydown(PRISM_CONTEXT, (button))
#define BUTTON_KEYUP(button) prism_button_keyup(PRISM_CONTEXT, (button))
#define engine_button_get_pressed_first()                                    \
  prism_button_first_pressed(PRISM_CONTEXT)
#define engine_button_held_ratio(button)                                     \
  prism_button_hold_ratio(PRISM_CONTEXT, (button))

#define platform_display_get_u8g2() prism_display(PRISM_CONTEXT)
#define platform_now_us() prism_now_us(PRISM_CONTEXT)
#define platform_time_diff_us(from, to)                                      \
  prism_time_diff_us(PRISM_CONTEXT, (from), (to))
#define engine_buttons_reset() prism_buttons_reset(PRISM_CONTEXT)
#define anim_to(subject, target, duration, easing, callback, user)            \
  prism_anim_to(PRISM_CONTEXT, (subject), (target), (duration), (easing),     \
                (callback), (user))
#define anim_cancel(subject, finish)                                          \
  prism_anim_cancel(PRISM_CONTEXT, (subject), (finish))
#define leaderboard_get_qrcode(app_id, data, data_len, out)                  \
  prism_leaderboard_qrcode(PRISM_CONTEXT, (app_id), (data), (data_len), (out))

static inline void cartridge_led_set_both(color_t color)
{
  prism_led_set(PRISM_CONTEXT, LED_L, color);
  prism_led_set(PRISM_CONTEXT, LED_R, color);
}
#define led_set_both(color) cartridge_led_set_both(color)
