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
  return (elm_t){.pos = aligned_pos, .u8g2 = parent->u8g2};
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
  {
    float draw_ratio = ease_out_cubic(ratio);
    uint16_t fill_width = (uint16_t)((width - 1) * draw_ratio);
    u8g2_SetDrawColor(u8g2, 2);
    if (fill_from_right)
      elm_box(&child, vec2(width - 1 - fill_width, 1), fill_width, height - 2);
    else
      elm_box(&child, vec2(1, 1), fill_width, height - 2);
  }

  if (pressed != NULL)
    *pressed = ratio >= 1.f;

  return child;
}
