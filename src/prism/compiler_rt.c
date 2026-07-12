#include <stddef.h>
#include <string.h>

/* Keep comparator callbacks inside the ARM guest. This intentionally favors a
 * tiny implementation over libc's faster one: cartridge collections are
 * small, and a native qsort could not invoke an ARM function pointer. */
void qsort(void *base, size_t count, size_t width,
           int (*compare)(const void *, const void *))
{
  if (base == NULL || compare == NULL || width == 0 || count < 2)
    return;

  unsigned char *bytes = base;
  for (size_t i = 1; i < count; ++i)
  {
    size_t j = i;
    while (j > 0 && compare(bytes + (j - 1) * width,
                            bytes + j * width) > 0)
    {
      for (size_t k = 0; k < width; ++k)
      {
        unsigned char temporary = bytes[(j - 1) * width + k];
        bytes[(j - 1) * width + k] = bytes[j * width + k];
        bytes[j * width + k] = temporary;
      }
      --j;
    }
  }
}
