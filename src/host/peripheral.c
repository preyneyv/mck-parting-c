#include <platform/peripheral.h>

static struct {
  bool enabled;
  bool plugged_in;
  bool charging_enabled;
  bool charging;
  uint8_t battery_level;
} g_peripheral;

void platform_peripheral_init(void) {
  g_peripheral.enabled = false;
  g_peripheral.plugged_in = true;
  g_peripheral.charging_enabled = false;
  g_peripheral.charging = false;
  g_peripheral.battery_level = 255;
}

void platform_peripheral_set_enabled(bool enabled) { g_peripheral.enabled = enabled; }

void platform_peripheral_set_charging_enabled(bool enabled) {
  g_peripheral.charging_enabled = enabled;
}

void platform_peripheral_read_inputs(void) {}

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
