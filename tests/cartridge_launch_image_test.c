#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <prism/package.h>
#include <shared/os/cartridge_image.h>

#define CHECK(condition)                                                     \
  do                                                                         \
  {                                                                          \
    if (!(condition))                                                        \
    {                                                                        \
      fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,    \
              #condition);                                                   \
      abort();                                                               \
    }                                                                        \
  } while (0)

enum
{
  GOT_OFFSET = PRISM_PACKAGE_HEADER_BYTES + 4 * sizeof(uint32_t),
  RW_OFFSET = GOT_OFFSET + 2 * sizeof(uint32_t),
  RW_INITIALIZED_BYTES = 4 * sizeof(uint32_t),
  RW_BYTES = RW_INITIALIZED_BYTES + 8192,
};

static void write_u32(uint8_t *bytes, size_t offset, uint32_t value)
{
  memcpy(bytes + offset, &value, sizeof(value));
}

static uint32_t read_u32(const uint8_t *bytes, size_t offset)
{
  uint32_t value;
  memcpy(&value, bytes + offset, sizeof(value));
  return value;
}

static void prepare(uint8_t *package, uint8_t *got, uint8_t *rw)
{
  prism_package_header_t *header = (void *)package;
  CHECK(prism_package_prepare_launch_image(
      package, header, 0x10000000u, got, 0x20000000u, rw, NULL, NULL));
}

int main(void)
{
  uint8_t package[RW_OFFSET + RW_INITIALIZED_BYTES] = {0};
  prism_package_header_t *header = (void *)package;
  header->image_size = 0x100;
  header->got_offset = GOT_OFFSET;
  header->got_size = 2 * sizeof(uint32_t);
  header->rw_offset = RW_OFFSET;
  header->rw_init_size = RW_INITIALIZED_BYTES;
  header->rw_size = RW_BYTES;
  header->relocations_offset = PRISM_PACKAGE_HEADER_BYTES;
  header->relocation_count = 4;

  prism_package_relocation_t *relocations =
      (void *)(package + header->relocations_offset);
  relocations[0].patch_offset = GOT_OFFSET;
  relocations[1].patch_offset = GOT_OFFSET + sizeof(uint32_t);
  relocations[2].patch_offset = RW_OFFSET + sizeof(uint32_t);
  relocations[3].patch_offset = RW_OFFSET + 2 * sizeof(uint32_t);

  write_u32(package, GOT_OFFSET, 0x20u);
  write_u32(package, GOT_OFFSET + sizeof(uint32_t), 0x108u);
  write_u32(package, RW_OFFSET, 0x12345678u);
  write_u32(package, RW_OFFSET + sizeof(uint32_t), 0x30u);
  write_u32(package, RW_OFFSET + 2 * sizeof(uint32_t), 0x100u);
  write_u32(package, RW_OFFSET + 3 * sizeof(uint32_t), 77u);

  uint8_t first_got[2 * sizeof(uint32_t)];
  uint8_t first_rw[RW_BYTES];
  prepare(package, first_got, first_rw);
  CHECK(read_u32(first_got, 0) == 0x10000020u);
  CHECK(read_u32(first_got, 4) == 0x20000008u);
  CHECK(read_u32(first_rw, 0) == 0x12345678u);
  CHECK(read_u32(first_rw, 4) == 0x10000030u);
  CHECK(read_u32(first_rw, 8) == 0x20000000u);
  CHECK(read_u32(first_rw, 12) == 77u);
  for (size_t i = RW_INITIALIZED_BYTES; i < sizeof(first_rw); ++i)
    CHECK(first_rw[i] == 0);

  memset(first_got, 0xff, sizeof(first_got));
  memset(first_rw, 0xff, sizeof(first_rw));

  uint8_t second_got[2 * sizeof(uint32_t)];
  uint8_t second_rw[RW_BYTES];
  prepare(package, second_got, second_rw);
  CHECK(read_u32(second_got, 0) == 0x10000020u);
  CHECK(read_u32(second_got, 4) == 0x20000008u);
  CHECK(read_u32(second_rw, 0) == 0x12345678u);
  CHECK(read_u32(second_rw, 4) == 0x10000030u);
  CHECK(read_u32(second_rw, 8) == 0x20000000u);
  CHECK(read_u32(second_rw, 12) == 77u);
  CHECK(second_rw[RW_BYTES - 1] == 0);

  puts("cartridge launch image tests passed");
  return 0;
}
