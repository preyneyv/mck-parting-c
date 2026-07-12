#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <qrcodegen.h>

static inline size_t prism_qrcode_xbm_bytes(uint16_t dimension)
{
  return (size_t)((dimension + 7u) / 8u) * dimension;
}

/* Convert qrcodegen's packed module grid into a solid XBM image. Light
 * modules and the quiet border are set; dark modules are clear. Drawing the
 * result in solid bitmap mode therefore replaces the full QR rectangle. */
static inline bool prism_qrcode_to_xbm(const uint8_t *qrcode, uint8_t border,
                                       uint8_t pixel_size, uint8_t *bitmap,
                                       size_t bitmap_capacity,
                                       uint16_t *dimension_out)
{
  if (qrcode == NULL || bitmap == NULL || dimension_out == NULL ||
      pixel_size == 0)
    return false;

  uint16_t size = qrcode[0];
  if (size < qrcodegen_VERSION_MIN * 4u + 17u ||
      size > qrcodegen_VERSION_MAX * 4u + 17u || (size - 17u) % 4u != 0)
    return false;

  uint32_t dimension = (uint32_t)size * pixel_size + 2u * border;
  if (dimension > UINT16_MAX)
    return false;

  uint16_t dim = (uint16_t)dimension;
  size_t stride = (dim + 7u) / 8u;
  size_t bitmap_bytes = stride * dim;
  if (bitmap_bytes > bitmap_capacity)
    return false;

  memset(bitmap, 0xff, bitmap_bytes);
  for (uint16_t module_y = 0; module_y < size; ++module_y)
    for (uint16_t module_x = 0; module_x < size; ++module_x)
    {
      uint32_t module_index = (uint32_t)module_y * size + module_x;
      if ((qrcode[(module_index >> 3) + 1] &
           (uint8_t)(1u << (module_index & 7u))) == 0)
        continue;

      uint16_t pixel_x = (uint16_t)(border + module_x * pixel_size);
      uint16_t pixel_y = (uint16_t)(border + module_y * pixel_size);
      for (uint8_t y = 0; y < pixel_size; ++y)
        for (uint8_t x = 0; x < pixel_size; ++x)
        {
          uint16_t output_x = (uint16_t)(pixel_x + x);
          bitmap[(size_t)(pixel_y + y) * stride + output_x / 8u] &=
              (uint8_t)~(1u << (output_x & 7u));
        }
    }

  *dimension_out = dim;
  return true;
}
