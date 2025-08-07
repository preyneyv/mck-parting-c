#include <stdio.h>

#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/watchdog.h>
#include <pico/stdio.h>

#include <shared/peripheral.h>

#include "config.h"

void peripheral_init(peripheral_t *p) {
  p->enabled = false;
  p->plugged_in = false;
  p->charging_enabled = true;
  p->charging = false;
  p->battery_level = 0;

  adc_init();

  gpio_init(PERIPH_PWR_EN);
  gpio_set_dir(PERIPH_PWR_EN, GPIO_OUT);
  gpio_put(PERIPH_PWR_EN, 0);

  gpio_init(PERIPH_BAT_CHG_EN_N);
  gpio_set_dir(PERIPH_BAT_CHG_EN_N, GPIO_OUT);
  gpio_put(PERIPH_BAT_CHG_EN_N, 0);

  gpio_init(PERIPH_VSYS_PGOOD_N);
  gpio_set_dir(PERIPH_VSYS_PGOOD_N, GPIO_IN);
  gpio_pull_up(PERIPH_VSYS_PGOOD_N);

  gpio_init(PERIPH_BAT_CHG_N);
  gpio_set_dir(PERIPH_BAT_CHG_N, GPIO_IN);
  gpio_pull_up(PERIPH_BAT_CHG_N);

  adc_gpio_init(PERIPH_VSYS);
}

void peripheral_set_enabled(peripheral_t *p, bool enabled) {
  p->enabled = enabled;
  gpio_put(PERIPH_PWR_EN, enabled ? 1 : 0);
}

void peripheral_set_charging_enabled(peripheral_t *p, bool enabled) {
  p->charging_enabled = enabled;
  gpio_put(PERIPH_BAT_CHG_EN_N, enabled ? 0 : 1); // active low
}

static const float ADC_TO_VOLTAGE =
    3.3f / (1 << 12); // 12-bit ADC resolution at 3.3VREF

static uint8_t battery_percentage_curve(uint16_t voltage_mV) {
  if (voltage_mV >= 4200)
    return 255;
  if (voltage_mV >= 4150)
    return 230 + (voltage_mV - 4150) * (25) / 50; // 4150–4200
  if (voltage_mV >= 4000)
    return 204 + (voltage_mV - 4000) * (26) / 150; // 4000–4150
  if (voltage_mV >= 3850)
    return 153 + (voltage_mV - 3850) * (51) / 150; // 3850–4000
  if (voltage_mV >= 3700)
    return 102 + (voltage_mV - 3700) * (51) / 150; // 3700–3850
  if (voltage_mV >= 3500)
    return 51 + (voltage_mV - 3500) * (51) / 200; // 3500–3700
  if (voltage_mV >= 3300)
    return (voltage_mV - 3300) * (51) / 200; // 3300–3500
  return 0;
}

void peripheral_read_inputs(peripheral_t *p) {
  // TODO: double check this works
  if (!p->enabled) {
    panic("cannot read peripheral inputs when not enabled");
  }

  p->charging = gpio_get(PERIPH_BAT_CHG_N) != 0;

  adc_select_input(PERIPH_VSYS_ADC);
  uint16_t raw_level = adc_read();
  float raw_voltage = (raw_level * ADC_TO_VOLTAGE);
  uint16_t bat_voltage =
      (raw_voltage * 1.51f) * 1000; // voltage divider 5.1k / 10k

  p->plugged_in = bat_voltage >= 4200; // 3.5V threshold for USB power
  p->battery_level = battery_percentage_curve(bat_voltage);
}
