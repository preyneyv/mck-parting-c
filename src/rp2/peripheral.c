#include <stdio.h>

#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <hardware/watchdog.h>
#include <pico/stdio.h>

#include <platform/peripheral.h>

#include "config.h"

static struct
{
  bool enabled;
  bool plugged_in;
  bool charging_enabled;
  bool charging;
  uint8_t battery_level;
} g_peripheral;

void platform_peripheral_init(void)
{
  g_peripheral.enabled = false;
  g_peripheral.plugged_in = false;
  g_peripheral.charging_enabled = true;
  g_peripheral.charging = false;
  g_peripheral.battery_level = 0;

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

void platform_peripheral_set_enabled(bool enabled)
{
  g_peripheral.enabled = enabled;
  gpio_put(PERIPH_PWR_EN, enabled ? 1 : 0);
}

void platform_peripheral_set_charging_enabled(bool enabled)
{
  g_peripheral.charging_enabled = enabled;
  gpio_put(PERIPH_BAT_CHG_EN_N, enabled ? 0 : 1); // active low
}

static const float ADC_TO_VOLTAGE =
    3.3f / (1 << 12); // 12-bit ADC resolution at 3.3VREF

static uint8_t battery_percentage_curve(uint16_t voltage_mV)
{
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

void platform_peripheral_read_inputs(void)
{
  g_peripheral.charging = !gpio_get(PERIPH_BAT_CHG_N);
  g_peripheral.plugged_in = !gpio_get(PERIPH_VSYS_PGOOD_N);

  const uint32_t AVG_SAMPLES = 2;
  if (g_peripheral.enabled)
  {
    adc_select_input(PERIPH_VSYS_ADC);
    uint32_t raw_level = 0;
    for (uint32_t i = 0; i < AVG_SAMPLES; i++)
      raw_level += adc_read();
    raw_level /= AVG_SAMPLES;
    float raw_voltage = (raw_level * ADC_TO_VOLTAGE);
    uint16_t bat_voltage =
        (raw_voltage * 1.51f) * 1000; // voltage divider 5.1k / 10k

    // p->plugged_in = bat_voltage >= 4250; // 3.5V threshold for USB power
    g_peripheral.battery_level = battery_percentage_curve(bat_voltage);
  }
  else
  {
    // can't read battery level if peripherals are off. report 0%.
    g_peripheral.battery_level = 0;
  }
}

platform_power_state_t platform_peripheral_get_power_state(void) {
  return (platform_power_state_t){
      .plugged_in = g_peripheral.plugged_in,
      .charging = g_peripheral.charging,
      .battery_level = g_peripheral.battery_level,
  };
}

bool platform_peripheral_is_enabled(void) { return g_peripheral.enabled; }
bool platform_peripheral_is_plugged_in(void) { return g_peripheral.plugged_in; }
bool platform_peripheral_is_charging_enabled(void) { return g_peripheral.charging_enabled; }
bool platform_peripheral_is_charging(void) { return g_peripheral.charging; }
uint8_t platform_peripheral_battery_level(void) { return g_peripheral.battery_level; }
