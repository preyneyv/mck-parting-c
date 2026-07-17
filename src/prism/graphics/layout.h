#pragma once

#include <u8g2.h>
#include <prism/graphics/display.h>
#include <prism/graphics/easing.h>
#include <prism/graphics/qrcode.h>
#include <prism/graphics/vector.h>
typedef struct
{
  vec2_t pos;
  u8g2_t *u8g2;
} elm_t;

typedef enum
{
  ELM_ALIGN_TOP_LEFT = 0,
  ELM_ALIGN_TOP_CENTER,
  ELM_ALIGN_TOP_RIGHT,
  ELM_ALIGN_CENTER_LEFT,
  ELM_ALIGN_CENTER,
  ELM_ALIGN_CENTER_RIGHT,
  ELM_ALIGN_BOTTOM_LEFT,
  ELM_ALIGN_BOTTOM_CENTER,
  ELM_ALIGN_BOTTOM_RIGHT,
} elm_align_t;

typedef struct
{
  uint8_t top;
  uint8_t right;
  uint8_t bottom;
  uint8_t left;
} elm_insets_t;

static inline vec2_t elm_aligned_position(vec2_t pos, uint16_t w, uint16_t h,
                                          elm_align_t align)
{
  vec2_t aligned_pos = pos;

  switch (align)
  {
  case ELM_ALIGN_TOP_LEFT:
    break;
  case ELM_ALIGN_TOP_CENTER:
    aligned_pos.x -= w / 2;
    break;
  case ELM_ALIGN_TOP_RIGHT:
    aligned_pos.x -= w;
    break;
  case ELM_ALIGN_CENTER_LEFT:
    aligned_pos.y -= h / 2;
    break;
  case ELM_ALIGN_CENTER:
    aligned_pos.x -= w / 2;
    aligned_pos.y -= h / 2;
    break;
  case ELM_ALIGN_CENTER_RIGHT:
    aligned_pos.x -= w;
    aligned_pos.y -= h / 2;
    break;
  case ELM_ALIGN_BOTTOM_LEFT:
    aligned_pos.y -= h;
    break;
  case ELM_ALIGN_BOTTOM_CENTER:
    aligned_pos.x -= w / 2;
    aligned_pos.y -= h;
    break;
  case ELM_ALIGN_BOTTOM_RIGHT:
    aligned_pos.x -= w;
    aligned_pos.y -= h;
    break;
  }

  return aligned_pos;
}

static inline elm_t elm_root(u8g2_t *u8g2, vec2_t pos)
{
  return (elm_t){.pos = pos, .u8g2 = u8g2};
}

static inline elm_t elm_child(elm_t *parent, vec2_t pos)
{
  return (elm_t){.pos = vec2_add(parent->pos, pos), .u8g2 = parent->u8g2};
}

static inline elm_t elm_child_aligned(elm_t *parent, vec2_t pos, uint16_t w,
                                      uint16_t h, elm_align_t align)
{
  vec2_t aligned_pos = elm_aligned_position(pos, w, h, align);
  return elm_child(parent, aligned_pos);
}

static inline elm_t elm_arc(elm_t *parent, vec2_t pos, uint16_t radius,
                            uint8_t start, uint8_t end)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawArc(child.u8g2, child.pos.x, child.pos.y, radius, start, end);
  return child;
}

static inline elm_t elm_pixel(elm_t *parent, vec2_t pos)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawPixel(child.u8g2, child.pos.x, child.pos.y);
  return child;
}

static inline elm_t elm_line(elm_t *parent, vec2_t p0, vec2_t p1)
{
  elm_t child = elm_child(parent, p0);
  u8g2_DrawLine(child.u8g2, child.pos.x, child.pos.y,
                child.pos.x + (p1.x - p0.x), child.pos.y + (p1.y - p0.y));
  return child;
}

static inline elm_t elm_box(elm_t *parent, vec2_t pos, uint16_t w, uint16_t h)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawBox(child.u8g2, child.pos.x, child.pos.y, w, h);
  return child;
}

static inline elm_t elm_frame(elm_t *parent, vec2_t pos, uint16_t w,
                              uint16_t h)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawFrame(child.u8g2, child.pos.x, child.pos.y, w, h);
  return child;
}

static inline elm_t elm_circle(elm_t *parent, vec2_t pos, uint16_t r,
                               uint8_t opt)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawCircle(child.u8g2, child.pos.x, child.pos.y, r, opt);
  return child;
}

static inline elm_t elm_disc(elm_t *parent, vec2_t pos, uint16_t r,
                             uint8_t opt)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawDisc(child.u8g2, child.pos.x, child.pos.y, r, opt);
  return child;
}

static inline elm_t elm_ellipse(elm_t *parent, vec2_t pos, uint16_t rx,
                                uint16_t ry, uint8_t opt)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawEllipse(child.u8g2, child.pos.x, child.pos.y, rx, ry, opt);
  return child;
}

static inline elm_t elm_filled_ellipse(elm_t *parent, vec2_t pos, uint16_t rx,
                                       uint16_t ry, uint8_t opt)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawFilledEllipse(child.u8g2, child.pos.x, child.pos.y, rx, ry, opt);
  return child;
}

static inline elm_t elm_triangle(elm_t *parent, vec2_t p0, vec2_t p1,
                                 vec2_t p2)
{
  elm_t child = elm_child(parent, p0);
  u8g2_DrawTriangle(child.u8g2, child.pos.x, child.pos.y,
                    child.pos.x + (p1.x - p0.x), child.pos.y + (p1.y - p0.y),
                    child.pos.x + (p2.x - p0.x), child.pos.y + (p2.y - p0.y));
  return child;
}

static inline elm_t elm_rounded_box(elm_t *parent, vec2_t pos, uint16_t w,
                                    uint16_t h, uint16_t r)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawRBox(child.u8g2, child.pos.x, child.pos.y, w, h, r);
  return child;
}

static inline elm_t elm_rounded_frame(elm_t *parent, vec2_t pos, uint16_t w,
                                      uint16_t h, uint16_t r)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawRFrame(child.u8g2, child.pos.x, child.pos.y, w, h, r);
  return child;
}

/* XOR-fill the rounded interior of a control without squaring off its corner
 * pixels. This is the shared visual treatment for Prism hold-to-act controls;
 * callers draw their frame and label first so both are inverted by the fill. */
static inline void elm_rounded_hold_fill(elm_t *parent, vec2_t pos,
                                         uint16_t width, uint16_t height,
                                         uint8_t radius, float ratio,
                                         bool from_right)
{
  if (ratio <= 0.f || width < 5 || height < 5)
    return;
  if (ratio > 1.f)
    ratio = 1.f;

  /* Match elm_btn and the launcher: animate a width that includes one border
   * pixel, then clip it to the rounded interior below. That makes the visible
   * interior finish slightly before the logical ratio reaches 1, preserving
   * the familiar short rejection window at the end of a hold. */
  uint16_t fill_span = width - 1u;
  uint16_t filled = (uint16_t)(fill_span * ease_out_cubic(ratio));
  if (filled == 0)
    return;

  u8g2_t *u8g2 = parent->u8g2;
  int16_t x = parent->pos.x + pos.x;
  int16_t y = parent->pos.y + pos.y;
  u8g2_SetDrawColor(u8g2, 2);
  for (uint16_t row = 0; row < height - 2u; ++row)
  {
    uint16_t edge_distance = row;
    uint16_t distance_from_bottom = height - 3u - row;
    if (distance_from_bottom < edge_distance)
      edge_distance = distance_from_bottom;
    uint16_t corner_rows = radius > 1u ? radius - 1u : 0u;
    uint16_t inset = edge_distance < corner_rows
                         ? (uint16_t)(corner_rows - edge_distance)
                         : (uint16_t)0;

    int16_t row_left = x + 1 + (int16_t)inset;
    int16_t row_right = x + (int16_t)width - 2 - (int16_t)inset;
    int16_t fill_left = from_right
                            ? x + (int16_t)width - 1 - (int16_t)filled
                            : x + 1;
    int16_t fill_right = from_right
                             ? x + (int16_t)width - 2
                             : x + (int16_t)filled;
    if (fill_left < row_left)
      fill_left = row_left;
    if (fill_right > row_right)
      fill_right = row_right;
    if (fill_right >= fill_left)
      u8g2_DrawHLine(u8g2, fill_left, y + 1 + row,
                     (uint16_t)(fill_right - fill_left + 1));
  }
  u8g2_SetDrawColor(u8g2, 1);
}

enum { ELM_INPUT_BADGE_OUTLINE_SCALE = 256 };

/* Large physical-input badge matching Prism's first-interaction screen.
 * `outline` is animated from 0 to ELM_INPUT_BADGE_OUTLINE_SCALE by the
 * caller; the echo grows symmetrically to a two-pixel outset. */
static inline elm_t elm_input_badge(elm_t *parent, vec2_t pos, uint16_t size,
                                    const char *label, int32_t outline,
                                    float ratio, bool from_right)
{
  elm_t child = elm_child(parent, pos);
  u8g2_t *u8g2 = child.u8g2;
  int16_t outset = (int16_t)((2 * outline +
                              ELM_INPUT_BADGE_OUTLINE_SCALE / 2) /
                             ELM_INPUT_BADGE_OUTLINE_SCALE);

  u8g2_SetDrawColor(u8g2, 1);
  if (outset != 0)
  {
    u8g2_DrawRFrame(u8g2, child.pos.x - outset,
                    child.pos.y - outset, size + 2u * outset,
                    size + 2u * outset, 2 + outset);
  }

  u8g2_DrawRFrame(u8g2, child.pos.x, child.pos.y, size, size, 2);
  u8g2_SetFont(u8g2, u8g2_font_7x14B_mr);
  uint16_t label_width = u8g2_GetStrWidth(u8g2, label);
  u8g2_DrawStr(u8g2, child.pos.x + (size - label_width) / 2,
               child.pos.y + (size + 10) / 2, label);
  elm_rounded_hold_fill(&child, VEC2_Z, size, size, 1, ratio, from_right);
  return child;
}

static inline elm_t elm_str(elm_t *parent, vec2_t pos, const char *str)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawStr(child.u8g2, child.pos.x, child.pos.y, str);
  return child;
}

/* Draw measured text in a padded frame. When filled, XOR only the interior so
 * the frame remains stable while the text becomes dark on a light field. The
 * caller selects the font and supplies its pixel height. */
static inline elm_t elm_boxed_text(elm_t *parent, vec2_t pos,
                                   const char *text, elm_align_t align,
                                   uint8_t text_height, elm_insets_t padding,
                                   bool filled)
{
  u8g2_t *u8g2 = parent->u8g2;
  uint16_t width = (uint16_t)(u8g2_GetStrWidth(u8g2, text) + padding.left +
                              padding.right + 2u);
  uint16_t height =
      (uint16_t)(text_height + padding.top + padding.bottom + 2u);
  elm_t child = elm_child_aligned(parent, pos, width, height, align);

  u8g2_SetDrawColor(u8g2, 1);
  elm_frame(&child, VEC2_Z, width, height);
  elm_str(&child,
          vec2((int16_t)(padding.left + 1u),
               (int16_t)(padding.top + text_height + 1u)),
          text);
  if (filled && width > 2 && height > 2)
  {
    u8g2_SetDrawColor(u8g2, 2);
    elm_box(&child, vec2(1, 1), width - 2u, height - 2u);
    u8g2_SetDrawColor(u8g2, 1);
  }
  return child;
}

static inline elm_t elm_rstr(elm_t *parent, vec2_t pos, const char *str)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawStr(child.u8g2, child.pos.x - u8g2_GetStrWidth(child.u8g2, str), child.pos.y, str);
  return child;
}

static inline elm_t elm_utf8(elm_t *parent, vec2_t pos, const char *str)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawUTF8(child.u8g2, child.pos.x, child.pos.y, str);
  return child;
}
static inline elm_t elm_vline(elm_t *parent, vec2_t pos, uint16_t h)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawVLine(child.u8g2, child.pos.x, child.pos.y, h);
  return child;
}

static inline elm_t elm_hline(elm_t *parent, vec2_t pos, uint16_t w)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawHLine(child.u8g2, child.pos.x, child.pos.y, w);
  return child;
}

static inline elm_t elm_xbm(elm_t *parent, vec2_t pos, uint16_t w, uint16_t h,
                            const uint8_t *bitmap)
{
  elm_t child = elm_child(parent, pos);
  u8g2_DrawXBM(child.u8g2, child.pos.x, child.pos.y, w, h, bitmap);
  return child;
}

static inline elm_t elm_qrcode(elm_t *parent, vec2_t pos,
                               elm_align_t align,
                               const uint8_t *qrcode, uint8_t border, uint8_t pixel_size)
{
  if (qrcode == NULL || pixel_size == 0)
    return elm_child_aligned(parent, pos, 0, 0, align);

  uint16_t size = qrcode[0];
  uint32_t dimension = (uint32_t)size * pixel_size + 2u * border;
  if (dimension > PRISM_DISPLAY_WIDTH)
    return elm_child_aligned(parent, pos, (uint16_t)dimension,
                             (uint16_t)dimension, align);

  uint16_t dim = (uint16_t)dimension;
  elm_t child = elm_child_aligned(parent, pos, dim, dim, align);

  size_t bitmap_bytes = prism_qrcode_xbm_bytes(dim);
  uint8_t bitmap[bitmap_bytes];
  uint16_t rendered_dim;
  if (!prism_qrcode_to_xbm(qrcode, border, pixel_size, bitmap,
                           bitmap_bytes, &rendered_dim))
    return child;

  /* Cartridge display pointers are opaque handles on the host VM. Keep this
   * helper on the public u8g2 function boundary instead of reading fields
   * from u8g2_t directly. Prism rendering normally uses solid bitmap mode,
   * and QR images require it so their light modules erase the background. */
  u8g2_SetDrawColor(child.u8g2, 1);
  u8g2_SetBitmapMode(child.u8g2, 0);
  u8g2_DrawXBM(child.u8g2, child.pos.x, child.pos.y, rendered_dim,
               rendered_dim, bitmap);
  return child;
}

static inline elm_t elm_btn(elm_t *parent, vec2_t pos, const char *label,
                            elm_align_t align, float ratio_l, float ratio_r,
                            bool *pressed)
{
  const uint8_t padding = 3;
  u8g2_t *u8g2 = parent->u8g2;

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_mr);

  uint32_t width = u8g2_GetStrWidth(u8g2, label) + padding * 2;
  uint32_t height = 7 + padding * 2;

  elm_t child = elm_child_aligned(parent, pos, width, height, align);

  elm_rounded_frame(&child, vec2(0, 0), width, height, 3);
  elm_str(&child, vec2(padding, 7 + padding), label);

  float ratio;
  bool fill_from_right = false;
  if (ratio_l > ratio_r)
    ratio = ratio_l;
  else
  {
    ratio = ratio_r;
    fill_from_right = true;
  }

  if (ratio > 0.f)
  {
    float draw_ratio = ease_out_cubic(ratio);
    u8g2_SetDrawColor(u8g2, 2);
    uint16_t fill_width = (uint16_t)((width - 1) * draw_ratio);
    if (fill_from_right)
    {
      u8g2_DrawBox(u8g2, child.pos.x + width - 1 - fill_width, child.pos.y + 1, fill_width, height - 2);
    }
    else
    {
      u8g2_DrawBox(u8g2, child.pos.x + 1, child.pos.y + 1, fill_width, height - 2);
    }
  }

  if (pressed != NULL)
  {
    *pressed = ratio >= 1.f;
  }

  return child;
}

// Fixed-width button driven by one physical input. Useful for layouts where
// the screen is spatially paired with the device's left and right buttons.
static inline elm_t elm_btn_input(elm_t *parent, vec2_t pos, uint16_t width,
                                  const char *label, elm_align_t align,
                                  float ratio, bool fill_from_right,
                                  bool *pressed)
{
  const uint8_t padding = 3;
  const uint16_t height = 8 + padding * 2;
  u8g2_t *u8g2 = parent->u8g2;

  u8g2_SetDrawColor(u8g2, 1);
  u8g2_SetFont(u8g2, u8g2_font_5x7_mr);

  elm_t child = elm_child_aligned(parent, pos, width, height, align);
  elm_rounded_frame(&child, vec2(0, 0), width, height, 3);

  uint16_t label_width = u8g2_GetStrWidth(u8g2, label);
  elm_str(&child, vec2((width - label_width) / 2, 7 + padding), label);

  if (ratio > 0.f)
    elm_rounded_hold_fill(&child, VEC2_Z, width, height, 2, ratio,
                          fill_from_right);

  if (pressed != NULL)
    *pressed = ratio >= 1.f;

  return child;
}
