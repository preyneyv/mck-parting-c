/**
 * core 0: game logic and ui
 * core 1: audio synthesis and playback
 */

#include <math.h>
#include <stdio.h>

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/irq.h>
#include <hardware/pll.h>
#include <hardware/pwm.h>
#include <hardware/resets.h>
#include <hardware/spi.h>
#include <hardware/structs/rosc.h>
#include <hardware/sync.h>
#include <hardware/watchdog.h>
#include <hardware/xosc.h>
#include <pico/multicore.h>
#include <pico/runtime_init.h>
#include <pico/stdlib.h>

#include <shared/engine.h>
#include <shared/utils/timing.h>

#include "audio.h"
#include "config.h"

void core1_main() {
  audio_playback_init();
  audio_playback_run_forever(&g_engine.synth);
}

void core0_main() { engine_run_forever(); }

void engine_buttons_init() {
  gpio_init(BUTTON_PIN_L);
  gpio_set_dir(BUTTON_PIN_L, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_L);

  gpio_init(BUTTON_PIN_R);
  gpio_set_dir(BUTTON_PIN_R, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_R);

  gpio_init(BUTTON_PIN_M);
  gpio_set_dir(BUTTON_PIN_M, GPIO_IN);
  gpio_pull_up(BUTTON_PIN_M);
}

bool engine_button_read(button_id_t button_id) {
  uint gpio;
  switch (button_id) {
  case BUTTON_LEFT:
    gpio = BUTTON_PIN_L;
    break;
  case BUTTON_RIGHT:
    gpio = BUTTON_PIN_R;
    break;
  case BUTTON_MENU:
    gpio = BUTTON_PIN_M;
    break;
  }
  return 1 - gpio_get(gpio); // active low
}

void measure_freqs(void) {
  uint f_pll_sys =
      frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
  uint f_pll_usb =
      frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY);
  uint f_rosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
  uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
  uint f_clk_peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
  uint f_clk_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
  uint f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
#ifdef CLOCKS_FC0_SRC_VALUE_CLK_RTC
  uint f_clk_rtc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_RTC);
#endif

  printf("pll_sys  = %dkHz\n", f_pll_sys);
  printf("pll_usb  = %dkHz\n", f_pll_usb);
  printf("rosc     = %dkHz\n", f_rosc);
  printf("clk_sys  = %dkHz\n", f_clk_sys);
  printf("clk_peri = %dkHz\n", f_clk_peri);
  printf("clk_usb  = %dkHz\n", f_clk_usb);
  printf("clk_adc  = %dkHz\n", f_clk_adc);
#ifdef CLOCKS_FC0_SRC_VALUE_CLK_RTC
  printf("clk_rtc  = %dkHz\n", f_clk_rtc);
#endif

  // Can't measure clk_ref / xosc as it is the ref
}

static critical_section_t sleep_critical_section;

inline static void rosc_write(io_rw_32 *addr, uint32_t value) {
  hw_clear_bits(&rosc_hw->status, ROSC_STATUS_BADWRITE_BITS);
  assert(!(rosc_hw->status & ROSC_STATUS_BADWRITE_BITS));
  *addr = value;
  assert(!(rosc_hw->status & ROSC_STATUS_BADWRITE_BITS));
}

void engine_sleep_until_interrupt() {
  critical_section_enter_blocking(&sleep_critical_section);
  sleep_ms(10);

  // rosc or xosc can be used. rosc is lower power.
  const bool use_xosc = false;

  // configure clocks
  uint src_hz;
  uint clk_ref_src;

  if (use_xosc) {
    src_hz = XOSC_HZ;
    clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC;
  } else {
    src_hz = 6500 * KHZ;
    clk_ref_src = CLOCKS_CLK_REF_CTRL_SRC_VALUE_ROSC_CLKSRC_PH;
  }
  clock_configure(clk_ref, clk_ref_src, 0, src_hz, src_hz);
  clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF, 0, src_hz,
                  src_hz);
  clock_stop(clk_adc);
  clock_stop(clk_usb);

  uint clk_rtc_src = use_xosc ? CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC
                              : CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_ROSC_CLKSRC_PH;
  clock_configure(clk_rtc, 0, clk_rtc_src, src_hz, 46875);
  clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                  src_hz, src_hz);
  pll_deinit(pll_sys);
  pll_deinit(pll_usb);

  // kill the unused oscillator
  if (use_xosc) {
    // disable rosc
    uint32_t tmp = rosc_hw->ctrl;
    tmp &= (~ROSC_CTRL_ENABLE_BITS);
    tmp |= (ROSC_CTRL_ENABLE_VALUE_DISABLE << ROSC_CTRL_ENABLE_LSB);
    rosc_write(&rosc_hw->ctrl, tmp);
    // Wait for stable to go away
    while (rosc_hw->status & ROSC_STATUS_STABLE_BITS)
      ;
  } else {

    xosc_disable();
  }

  // enter sleep
  uint32_t event = IO_BANK0_DORMANT_WAKE_INTE0_GPIO0_EDGE_LOW_BITS;

  gpio_set_dormant_irq_enabled(BUTTON_PIN_L, event, true);
  gpio_set_dormant_irq_enabled(BUTTON_PIN_R, event, true);
  gpio_set_dormant_irq_enabled(BUTTON_PIN_M, event, true);

  if (use_xosc) {
    xosc_dormant();
  } else {
    rosc_write(&rosc_hw->dormant, ROSC_DORMANT_VALUE_DORMANT);
    while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS))
      ;
  }

  gpio_acknowledge_irq(BUTTON_PIN_L, event);
  gpio_acknowledge_irq(BUTTON_PIN_R, event);
  gpio_acknowledge_irq(BUTTON_PIN_M, event);

  // wake from sleep
  // - enable rosc
  rosc_write(&rosc_hw->ctrl, ROSC_CTRL_ENABLE_BITS);
  rosc_hw->ctrl = ROSC_CTRL_ENABLE_BITS;
  while (!(rosc_hw->status & ROSC_STATUS_STABLE_BITS))
    ;

  // - reset clock flags
  clocks_hw->sleep_en0 |= ~(0u);
  clocks_hw->sleep_en1 |= ~(0u);

  // - reset clock speeds (this will wake xosc)
  clocks_init();
  set_sys_clock_hz(SYS_CLOCK_HZ, true);

  sleep_ms(10);
  critical_section_exit(&sleep_critical_section);
}

int main() {
  // hack to limit power draw until after usb stack is initialized
  // since macos doesn't like it when the usb device draws too much power
  // immediately.
  // todo: see if we can do this better
  gpio_init(PERIPH_BAT_CHG_EN_N);
  gpio_set_dir(PERIPH_BAT_CHG_EN_N, GPIO_OUT);
  gpio_put(PERIPH_BAT_CHG_EN_N, 1);
  sleep_ms(1000);

  // set clock for audio and LED timing reasons
  set_sys_clock_hz(SYS_CLOCK_HZ, true);
  stdio_init_all();

  printf("prism v1\n");
  critical_section_init(&sleep_critical_section);
  engine_init();

  // start audio core
  multicore_reset_core1();
  multicore_launch_core1(core1_main);

  // start main core
  core0_main();
  return 0;
}
