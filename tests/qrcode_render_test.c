#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <prism/graphics/qrcode.h>

#define CHECK(condition)                                                       \
  do                                                                           \
  {                                                                            \
    if (!(condition))                                                          \
      return __LINE__;                                                         \
  } while (0)

static void set_module(uint8_t *qrcode, uint8_t size, uint8_t x, uint8_t y)
{
  uint16_t index = (uint16_t)y * size + x;
  qrcode[(index >> 3) + 1] |= (uint8_t)(1u << (index & 7u));
}

static bool xbm_pixel(const uint8_t *bitmap, uint16_t dimension, uint16_t x,
                      uint16_t y)
{
  size_t stride = (dimension + 7u) / 8u;
  return (bitmap[(size_t)y * stride + x / 8u] &
          (uint8_t)(1u << (x & 7u))) != 0;
}

int main(void)
{
  enum
  {
    QR_SIZE = 21,
    BORDER = 2,
    SCALE = 2,
    DIMENSION = QR_SIZE * SCALE + BORDER * 2,
  };
  uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(1)] = {QR_SIZE};
  set_module(qrcode, QR_SIZE, 0, 0);
  set_module(qrcode, QR_SIZE, 10, 5);
  set_module(qrcode, QR_SIZE, 20, 20);

  uint8_t bitmap[((DIMENSION + 7) / 8) * DIMENSION];
  uint16_t dimension = 0;
  CHECK(prism_qrcode_to_xbm(qrcode, BORDER, SCALE, bitmap, sizeof(bitmap),
                            &dimension));
  CHECK(dimension == DIMENSION);
  CHECK(prism_qrcode_xbm_bytes(dimension) == sizeof(bitmap));

  for (uint16_t y = 0; y < dimension; ++y)
    for (uint16_t x = 0; x < dimension; ++x)
    {
      bool dark =
          (x >= 2 && x < 4 && y >= 2 && y < 4) ||
          (x >= 22 && x < 24 && y >= 12 && y < 14) ||
          (x >= 42 && x < 44 && y >= 42 && y < 44);
      CHECK(xbm_pixel(bitmap, dimension, x, y) != dark);
    }

  CHECK(!prism_qrcode_to_xbm(qrcode, BORDER, SCALE, bitmap,
                             sizeof(bitmap) - 1u, &dimension));
  CHECK(!prism_qrcode_to_xbm(qrcode, BORDER, 0, bitmap, sizeof(bitmap),
                             &dimension));

  enum
  {
    VERSION_4_SIZE = 33,
    VERSION_4_DIMENSION = VERSION_4_SIZE + BORDER * 2,
  };
  uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
  uint8_t encoded[qrcodegen_BUFFER_LEN_FOR_VERSION(4)];
  CHECK(qrcodegen_encodeText("HTTPS://PRISM.PREYNEYV.DEV/L/TEST", scratch,
                             encoded, qrcodegen_Ecc_LOW, 4, 4,
                             qrcodegen_Mask_AUTO, true));
  uint8_t rendered[((VERSION_4_DIMENSION + 7) / 8) *
                   VERSION_4_DIMENSION];
  CHECK(prism_qrcode_to_xbm(encoded, BORDER, 1, rendered, sizeof(rendered),
                            &dimension));
  CHECK(dimension == VERSION_4_DIMENSION);
  for (uint16_t y = 0; y < dimension; ++y)
    for (uint16_t x = 0; x < dimension; ++x)
    {
      bool inside = x >= BORDER && x < BORDER + VERSION_4_SIZE &&
                    y >= BORDER && y < BORDER + VERSION_4_SIZE;
      bool dark = inside && qrcodegen_getModule(
                                  encoded, x - BORDER, y - BORDER);
      CHECK(xbm_pixel(rendered, dimension, x, y) != dark);
    }
  return 0;
}
