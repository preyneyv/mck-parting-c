#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <pico/time.h>

#include <platform/peripheral.h>

#include "config.h"

static struct
{
  bool enabled;
  bool plugged_in;
  bool charging_enabled;
  bool charging;
  uint8_t battery_level;
  bool battery_filter_initialized;
  uint32_t filtered_raw_q8;
  uint64_t next_battery_sample_us;
} g_peripheral;

enum
{
  BATTERY_SAMPLE_INTERVAL_US = 250000,
  BATTERY_EMA_RESET_DELAY_US = 250000,
  BATTERY_OVERSAMPLE_COUNT = 16,
  BATTERY_FILTER_DIVISOR = 8,
};

void platform_peripheral_init(void)
{
  g_peripheral.enabled = false;
  g_peripheral.plugged_in = false;
  g_peripheral.charging_enabled = true;
  g_peripheral.charging = false;
  g_peripheral.battery_level = 0;
  g_peripheral.battery_filter_initialized = false;
  g_peripheral.filtered_raw_q8 = 0;
  g_peripheral.next_battery_sample_us = 0;

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
  if (enabled && !g_peripheral.enabled)
  {
    g_peripheral.battery_filter_initialized = false;
    g_peripheral.next_battery_sample_us =
        time_us_64() + BATTERY_EMA_RESET_DELAY_US;
  }
  g_peripheral.enabled = enabled;
  gpio_put(PERIPH_PWR_EN, enabled ? 1 : 0);
}

void platform_peripheral_set_charging_enabled(bool enabled)
{
  g_peripheral.charging_enabled = enabled;
  gpio_put(PERIPH_BAT_CHG_EN_N, enabled ? 0 : 1);
}

static uint8_t battery_percentage_curve(uint16_t voltage_mV)
{
  if (voltage_mV >= 4000)
    return 255;
  if (voltage_mV >= 3850)
    return 153 + (voltage_mV - 3850) * 102 / 150;
  if (voltage_mV >= 3700)
    return 102 + (voltage_mV - 3700) * 51 / 150;
  if (voltage_mV >= 3500)
    return 51 + (voltage_mV - 3500) * 51 / 200;
  if (voltage_mV >= 3300)
    return (voltage_mV - 3300) * 51 / 200;
  return 0;
}

void platform_peripheral_read_inputs(void)
{
  bool plugged_in = !gpio_get(PERIPH_VSYS_PGOOD_N);
  if (plugged_in != g_peripheral.plugged_in)
  {
    /* External power masks the battery voltage on VSYS. Preserve the last
     * battery estimate while plugged in, then restart from a fresh sample
     * after unplugging. */
    if (!plugged_in)
    {
      g_peripheral.battery_filter_initialized = false;
      g_peripheral.next_battery_sample_us =
          time_us_64() + BATTERY_EMA_RESET_DELAY_US;
    }
  }
  g_peripheral.charging = !gpio_get(PERIPH_BAT_CHG_N);
  g_peripheral.plugged_in = plugged_in;

  /* VSYS reflects external power while plugged in, not battery voltage. Keep
   * the last battery estimate and take a fresh immediate sample on unplug. */
  if (!g_peripheral.enabled || plugged_in)
    return;

  uint64_t now_us = time_us_64();
  if (g_peripheral.next_battery_sample_us != 0 &&
      now_us < g_peripheral.next_battery_sample_us)
    return;
  g_peripheral.next_battery_sample_us =
      now_us + BATTERY_SAMPLE_INTERVAL_US;

  adc_select_input(PERIPH_VSYS_ADC);
  (void)adc_read(); /* Discard the first conversion after selecting input. */
  uint32_t raw_sum = 0;
  for (uint32_t i = 0; i < BATTERY_OVERSAMPLE_COUNT; ++i)
    raw_sum += adc_read();
  uint32_t raw_average =
      (raw_sum + BATTERY_OVERSAMPLE_COUNT / 2u) / BATTERY_OVERSAMPLE_COUNT;
  uint32_t raw_q8 = raw_average << 8;

  if (!g_peripheral.battery_filter_initialized)
  {
    g_peripheral.filtered_raw_q8 = raw_q8;
    g_peripheral.battery_filter_initialized = true;
  }
  else
  {
    int32_t difference =
        (int32_t)raw_q8 - (int32_t)g_peripheral.filtered_raw_q8;
    g_peripheral.filtered_raw_q8 = (uint32_t)(
        (int32_t)g_peripheral.filtered_raw_q8 +
        difference / BATTERY_FILTER_DIVISOR);
  }

  uint32_t filtered_raw = (g_peripheral.filtered_raw_q8 + 128u) >> 8;
  uint16_t battery_voltage =
      (uint16_t)(filtered_raw * 4983u / 4096u);
  g_peripheral.battery_level = battery_percentage_curve(battery_voltage);
}
platform_power_state_t platform_peripheral_get_power_state(void)
{
  return (platform_power_state_t){
      .plugged_in = g_peripheral.plugged_in,
      .charging = g_peripheral.charging,
      .battery_level = g_peripheral.battery_level,
  };
}
