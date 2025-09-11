// elm is short for element
// a super basic hierarchical wrapper around u8g2

#pragma once

#include <shared/utils/vec.h>
#include <u8g2.h>

typedef struct {
  vec2_t pos;
  u8g2_t *u8g2;
} elm_t;

static inline elm_t elm_root(u8g2_t *u8g2, vec2_t pos) {
  return (elm_t){.pos = pos, .u8g2 = u8g2};
}

static inline elm_t elm_child(elm_t *parent, vec2_t pos) {
  return (elm_t){.pos = vec2_add(parent->pos, pos), .u8g2 = parent->u8g2};
}

static inline elm_t elm_arc(elm_t *parent, vec2_t pos, uint16_t radius,
                            uint8_t start, uint8_t end) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawArc(child.u8g2, child.pos.x, child.pos.y, radius, start, end);
  return child;
}

static inline elm_t elm_pixel(elm_t *parent, vec2_t pos) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawPixel(child.u8g2, child.pos.x, child.pos.y);
  return child;
}

static inline elm_t elm_line(elm_t *parent, vec2_t p0, vec2_t p1) {
  elm_t child = elm_child(parent, p0);
  u8g2_DrawLine(child.u8g2, child.pos.x, child.pos.y,
                child.pos.x + (p1.x - p0.x), child.pos.y + (p1.y - p0.y));
  return child;
}

static inline elm_t elm_box(elm_t *parent, vec2_t pos, uint16_t w, uint16_t h) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawBox(child.u8g2, child.pos.x, child.pos.y, w, h);
  return child;
}

static inline elm_t elm_frame(elm_t *parent, vec2_t pos, uint16_t w,
                              uint16_t h) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawFrame(child.u8g2, child.pos.x, child.pos.y, w, h);
  return child;
}

static inline elm_t elm_circle(elm_t *parent, vec2_t pos, uint16_t r,
                               uint8_t opt) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawCircle(child.u8g2, child.pos.x, child.pos.y, r, opt);
  return child;
}

static inline elm_t elm_disc(elm_t *parent, vec2_t pos, uint16_t r,
                             uint8_t opt) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawDisc(child.u8g2, child.pos.x, child.pos.y, r, opt);
  return child;
}

static inline elm_t elm_ellipse(elm_t *parent, vec2_t pos, uint16_t rx,
                                uint16_t ry, uint8_t opt) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawEllipse(child.u8g2, child.pos.x, child.pos.y, rx, ry, opt);
  return child;
}

static inline elm_t elm_filled_ellipse(elm_t *parent, vec2_t pos, uint16_t rx,
                                       uint16_t ry, uint8_t opt) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawFilledEllipse(child.u8g2, child.pos.x, child.pos.y, rx, ry, opt);
  return child;
}

static inline elm_t elm_triangle(elm_t *parent, vec2_t p0, vec2_t p1,
                                 vec2_t p2) {
  elm_t child = elm_child(parent, p0);
  u8g2_DrawTriangle(child.u8g2, child.pos.x, child.pos.y,
                    child.pos.x + (p1.x - p0.x), child.pos.y + (p1.y - p0.y),
                    child.pos.x + (p2.x - p0.x), child.pos.y + (p2.y - p0.y));
  return child;
}

static inline elm_t elm_rounded_box(elm_t *parent, vec2_t pos, uint16_t w,
                                    uint16_t h, uint16_t r) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawRBox(child.u8g2, child.pos.x, child.pos.y, w, h, r);
  return child;
}

static inline elm_t elm_rounded_frame(elm_t *parent, vec2_t pos, uint16_t w,
                                      uint16_t h, uint16_t r) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawRFrame(child.u8g2, child.pos.x, child.pos.y, w, h, r);
  return child;
}

static inline elm_t elm_str(elm_t *parent, vec2_t pos, const char *str) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawStr(child.u8g2, child.pos.x, child.pos.y, str);
  return child;
}

static inline elm_t elm_utf8(elm_t *parent, vec2_t pos, const char *str) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawUTF8(child.u8g2, child.pos.x, child.pos.y, str);
  return child;
}
static inline elm_t elm_vline(elm_t *parent, vec2_t pos, uint16_t h) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawVLine(child.u8g2, child.pos.x, child.pos.y, h);
  return child;
}

static inline elm_t elm_hline(elm_t *parent, vec2_t pos, uint16_t w) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawHLine(child.u8g2, child.pos.x, child.pos.y, w);
  return child;
}

static inline elm_t elm_xbm(elm_t *parent, vec2_t pos, uint16_t w, uint16_t h,
                            const uint8_t *bitmap) {
  elm_t child = elm_child(parent, pos);
  u8g2_DrawXBM(child.u8g2, child.pos.x, child.pos.y, w, h, bitmap);
  return child;
}
