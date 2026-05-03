/**
 * pwr_button.cpp
 * Gestion du bouton PWR via AXP2101 (XPowersLib 0.3.3).
 *
 * Fix v5 :
 *  - XPowersPMU -> XPowersAXP2101 (correct pour XPowersLib 0.3.3)
 *  - Utilise AXP_INT depuis pin_config.h
 *  - shutdown via pmu.shutdown()
 *  - pmu_get_battery_percent() robuste
 */
#include "pwr_button.h"
#include "bootloader_ui.h"
#include "display_init.h"
#include "pin_config.h"
#include <XPowersLib.h>
#include <Arduino.h>
#include <esp_sleep.h>

#define AXP_I2C_ADDR  0x34

static XPowersAXP2101 pmu;
static bool            _pmu_ok   = false;
static bool            _sleeping = false;

static volatile bool _irq_fired = false;
static uint32_t      _press_ms  = 0;
static bool          _pressed   = false;

static void IRAM_ATTR axp_isr() {
  _irq_fired = true;
}

static void enter_sleep() {
  Serial.println("[PWR] Veille...");
  _sleeping = true;
  extern Arduino_CO5300 *gfx;
  gfx->setBrightness(0);
  gfx->displayOff();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)AXP_INT, 0);
  esp_light_sleep_start();
  // Reveil
  Serial.println("[PWR] Reveil.");
  gfx->displayOn();
  gfx->setBrightness(200);
  _sleeping = false;
  if (_pmu_ok) pmu.clearIrqStatus();
  _irq_fired = false;
  _pressed   = false;
}

static void power_off() {
  Serial.println("[PWR] Arret complet.");
  delay(200);
  if (_pmu_ok) {
    pmu.shutdown();
  }
  esp_deep_sleep_start();
}

void pwr_button_init() {
  _pmu_ok = pmu.begin(Wire, AXP_I2C_ADDR, IIC_SDA, IIC_SCL);
  if (!_pmu_ok) {
    Serial.println("[PWR] AXP2101 non trouve - bouton PWR desactive");
    return;
  }
  Serial.println("[PWR] AXP2101 OK");

  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.enableIRQ(
    XPOWERS_AXP2101_PKEY_SHORT_IRQ    |
    XPOWERS_AXP2101_PKEY_LONG_IRQ     |
    XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
    XPOWERS_AXP2101_PKEY_POSITIVE_IRQ
  );
  pmu.clearIrqStatus();

#ifdef XPOWERS_AXP2101_POWEROFF_6S
  pmu.setPowerKeyPressOffTime(XPOWERS_AXP2101_POWEROFF_6S);
#endif

  pinMode(AXP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(AXP_INT), axp_isr, FALLING);
}

void pwr_button_tick() {
  if (!_pmu_ok) return;

  bool pin_low = (digitalRead(AXP_INT) == LOW);
  if (pin_low && !_pressed) {
    _pressed  = true;
    _press_ms = millis();
  } else if (!pin_low && _pressed) {
    _pressed = false;
    uint32_t held = millis() - _press_ms;
    Serial.printf("[PWR] Relache apres %lu ms\n", held);
    if (held >= 5000) {
      power_off();
      return;
    } else if (held >= 1500) {
      if (!_sleeping) enter_sleep();
      return;
    } else if (held >= 50) {
      if (!_sleeping) {
        Serial.println("[PWR] Retour carousel");
        bootloader_ui_return();
      }
    }
  }

  if (!_irq_fired) return;
  _irq_fired = false;
  pmu.getIrqStatus();

  if (pmu.isPekeyShortPressIrq()) {
    if (!_sleeping) bootloader_ui_return();
  }
  if (pmu.isPekeyLongPressIrq()) {
    if (!_sleeping) enter_sleep();
  }

  pmu.clearIrqStatus();
}

int pmu_get_battery_percent() {
  if (!_pmu_ok) return -1;
  if (!pmu.isBatteryConnect()) return -1;
  int pct = (int)pmu.getBatteryPercent();
  if (pct < 0 || pct > 100) return -1;
  return pct;
}
