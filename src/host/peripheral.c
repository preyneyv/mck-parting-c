#include <shared/peripheral.h>

void peripheral_init(peripheral_t *p) {
  p->enabled = false;
  p->plugged_in = true;
  p->charging_enabled = true;
  p->charging = false;
  p->battery_level = 100;
}

void peripheral_set_enabled(peripheral_t *p, bool enabled) {
  p->enabled = enabled;
}

void peripheral_set_charging_enabled(peripheral_t *p, bool enabled) {
  p->charging_enabled = enabled;
}

void peripheral_read_inputs(peripheral_t *p) {
  (void)p;
}
