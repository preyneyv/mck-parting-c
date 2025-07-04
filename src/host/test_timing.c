#include <stdio.h>

#include <shared/utils/timing.h>

int main() {
  TimingInstrumenter ti;

  uint32_t i = 0;

  while (1) {
    i += 1;
    if (i > 100) {
      printf("avg: %f\n", ti_get_average_ms(&ti, true));
      i = 0;
    }
    ti_start(&ti);
    sleep_us(1331);
    ti_stop(&ti);
  }
}
