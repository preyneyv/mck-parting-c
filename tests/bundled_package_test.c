#include "shared/os/bundled_package.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
  assert(!prism_bundled_package_should_replace(3, 100, 0x1234, 2, 200,
                                                0x5678));
  assert(prism_bundled_package_should_replace(2, 100, 0x1234, 3, 100,
                                               0x1234));
  assert(!prism_bundled_package_should_replace(3, 100, 0x1234, 3, 100,
                                                0x1234));
  assert(prism_bundled_package_should_replace(3, 100, 0x1234, 3, 100,
                                               0x5678));
  assert(prism_bundled_package_should_replace(3, 100, 0x1234, 3, 101,
                                               0x1234));
  puts("bundled package reconciliation tests passed");
  return 0;
}
