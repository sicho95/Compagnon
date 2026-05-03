/**
 * pwr_button.cpp
 * Gestion du bouton PWR via AXP2101 (XPowersLib 0.3.3).
 *
 * Fix v3 :
 *  - getIrqStatus() au lieu de readIrqStatus()
 *  - XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ  (sans _EDGE_)
 *  - XPOWERS_AXP2101_PKEY_POSITIVE_IRQ  (sans _EDGE_)
 *  - isPekeyNegativeIrq()  (pas NegativeTrigger, pas NegativeEdgeTrigger)
 *  - isPekeyPositiveIrq()  (pas PositiveTrigger, pas PositiveEdgeTrigger)
 *
 * Comportements :
 *   Appui court  (< 2 s)  -> retour au carousel launcher
 *   Appui long   (>= 2 s) -> veille ecran
 *   Appui tres long (>= 5 s) -> arret complet via AXP2101 shutdown
 */
#include "pwr_button.h"
#include "bootloader_ui.h"
#include "display_init.h"
#include <XPowersLib.h>
#include <Arduino.h>
#include <esp_sleep.h>

#define AXP_IRQ_PIN   13
#define AXP_I2C_ADDR  0x34

static XPowersPMU pmu;
static bool       _pmu_ok   = false;
static bool       _sleeping = false;

static volatile uint32_t _press_start_ms = 0;
static volatile bool     _irq_fired      = false;

static void IRAM_ATTR axp_isr() {
  _irq_fired = true;
}

static void enter_sleep() {
  Serial.println("[PWR] Veille...");
  _sleeping = true;
  extern Arduino_CO5300 *gfx;
  gfx->setBrightness(0);
  gfx->displayOff();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)AXP_IRQ_PIN, 0);
  esp_light_sleep_start();
  Serial.println("[PWR] Reveil.");
  gfx->displayOn();
  gfx->setBrightness(200);
  _sleeping = false;
  if (_pmu_ok) pmu.clearIrqStatus();
  _irq_fired      = false;
  _press_start_ms = millis();
}

static void power_off() {
  Serial.println("[PWR] Arret complet.");
  delay(100);
  if (_pmu_ok) pmu.shutdown();
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
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ   |
                XPOWERS_AXP2101_PKEY_LONG_IRQ     |
                XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
                XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);
  pmu.clearIrqStatus();

  pinMode(AXP_IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(AXP_IRQ_PIN), axp_isr, FALLING);
  _press_start_ms = millis();
}

void pwr_button_tick() {
  if (!_pmu_ok || !_irq_fired) return;
  _irq_fired = false;

  pmu.getIrqStatus();
  uint32_t now = millis();

  // Noms exacts XPowersLib 0.3.3
  if (pmu.isPekeyNegativeIrq()) {
    _press_start_ms = now;
    Serial.println("[PWR] Bouton enfonce");
  }

  if (pmu.isPekeyPositiveIrq()) {
    uint32_t held = now - _press_start_ms;
    Serial.printf("[PWR] Relache apres %lu ms\n", held);

    if (held >= 5000) {
      power_off();
    } else if (held >= 2000) {
      enter_sleep();
    } else {
      if (!_sleeping) {
        Serial.println("[PWR] Retour au carousel");
        bootloader_ui_return();
      }
    }
  }

  pmu.clearIrqStatus();
}

int pmu_get_battery_percent() {
  if (!_pmu_ok) return -1;
  if (!pmu.isBatteryConnect()) return -1;
  return (int)pmu.getBatteryPercent();
}
