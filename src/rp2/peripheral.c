#include <hardware/gpio.h>

#include <shared/peripheral.h>

#include "config.h"

void peripheral_init(peripheral_t *p) {
  p->enabled = false;

  gpio_init(PERIPH_PWR_EN);
  gpio_set_dir(PERIPH_PWR_EN, GPIO_OUT);
  gpio_put(PERIPH_PWR_EN, 0);

  // todo: charge control for battery.
  gpio_init(PERIPH_BAT_CHG_EN_N);
  gpio_set_dir(PERIPH_BAT_CHG_EN_N, GPIO_OUT);
  gpio_put(PERIPH_BAT_CHG_EN_N, 0);

  gpio_init(PERIPH_VSYS_PGOOD_N);
  gpio_set_dir(PERIPH_VSYS_PGOOD_N, GPIO_IN);
  gpio_pull_up(PERIPH_VSYS_PGOOD_N);

  gpio_init(PERIPH_BAT_CHG_N);
  gpio_set_dir(PERIPH_BAT_CHG_N, GPIO_IN);
  gpio_pull_up(PERIPH_BAT_CHG_N);
}

void peripheral_set_enabled(peripheral_t *p, bool enabled) {
  p->enabled = enabled;
  gpio_put(PERIPH_PWR_EN, enabled ? 1 : 0);
}
