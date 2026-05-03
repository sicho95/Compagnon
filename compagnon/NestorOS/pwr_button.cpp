/**
 * pwr_button.cpp
 * Gestion du bouton PWR via AXP2101 (I2C, XPowersLib).
 *
 * Comportements :
 *   Appui court  (< 1 s)  -> retour au carousel launcher
 *   Appui long   (>= 2 s) -> veille ecran (AMOLED off + light sleep)
 *   Appui tres long (>= 5 s) -> arret complet via AXP2101 power off
 *
 * L'AXP2101 genere une interruption sur son pin IRQ a chaque
 * changement d'etat du bouton PWR. On mesure la duree au niveau C++.
 *
 * Cablage Waveshare ESP32-S3-Touch-AMOLED-2.16 :
 *   AXP2101 SDA = GPIO15, SCL = GPIO14 (bus Wire deja init)
 *   AXP2101 IRQ = GPIO13  (interrupt active-low)
 */
#include "pwr_button.h"
#include "bootloader_ui.h"
#include "display_init.h"
#include <XPowersLib.h>
#include <Arduino.h>
#include <esp_sleep.h>

// GPIO IRQ AXP2101 sur la Waveshare 2.16"
#define AXP_IRQ_PIN   13
#define AXP_I2C_ADDR  0x34

static XPowersPMU pmu;
static bool       _pmu_ok    = false;
static bool       _sleeping  = false;

// Timestamp du dernier front descendant (bouton enfonce)
static volatile uint32_t _press_start_ms = 0;
static volatile bool     _irq_fired      = false;

// ── ISR legere ──────────────────────────────────────────────────────────────
static void IRAM_ATTR axp_isr() {
  _irq_fired = true;
}

// ── Veille ecran ────────────────────────────────────────────────────────────
static void enter_sleep() {
  Serial.println("[PWR] Veille...");
  _sleeping = true;

  // Eteint l'ecran AMOLED via GFX
  extern Arduino_CO5300 *gfx;
  gfx->setBrightness(0);
  gfx->displayOff();

  // Light sleep ESP32-S3 (reveil sur IRQ AXP)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)AXP_IRQ_PIN, 0); // reveil sur LOW
  esp_light_sleep_start();

  // === Reveil ===
  Serial.println("[PWR] Reveil.");
  gfx->displayOn();
  gfx->setBrightness(200);
  _sleeping = false;

  // Vide l'IRQ pour eviter une action immediate apres le reveil
  if (_pmu_ok) pmu.clearIrqStatus();
  _irq_fired    = false;
  _press_start_ms = millis();
}

// ── Arret complet ────────────────────────────────────────────────────────────
static void power_off() {
  Serial.println("[PWR] Arret complet.");
  delay(100);
  if (_pmu_ok) pmu.shutdown();  // coupe l'alim via AXP2101
  // Fallback si AXP indisponible
  esp_deep_sleep_start();
}

// ── Init ─────────────────────────────────────────────────────────────────────
void pwr_button_init() {
  // Init AXP2101 (Wire deja demarre dans display_init)
  _pmu_ok = pmu.begin(Wire, AXP_I2C_ADDR, IIC_SDA, IIC_SCL);
  if (!_pmu_ok) {
    Serial.println("[PWR] AXP2101 non trouve - bouton PWR desactive");
    return;
  }
  Serial.println("[PWR] AXP2101 OK");

  // Active l'IRQ sur appui/relachement du bouton PWR
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ   |
                XPOWERS_AXP2101_PKEY_LONG_IRQ     |
                XPOWERS_AXP2101_PKEY_NEGATIVE_EDGE_IRQ |
                XPOWERS_AXP2101_PKEY_POSITIVE_EDGE_IRQ);
  pmu.clearIrqStatus();

  // Config GPIO IRQ
  pinMode(AXP_IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(AXP_IRQ_PIN), axp_isr, FALLING);

  _press_start_ms = millis();
}

// ── Tick (loop) ──────────────────────────────────────────────────────────────
void pwr_button_tick() {
  if (!_pmu_ok || !_irq_fired) return;
  _irq_fired = false;

  pmu.readIrqStatus();
  uint32_t now = millis();

  // ── Front descendant : bouton enfonce ──
  if (pmu.isPekeyNegativeEdgeTrigger()) {
    _press_start_ms = now;
    Serial.println("[PWR] Bouton enfonce");
  }

  // ── Front montant : bouton relache ──
  if (pmu.isPekeyPositiveEdgeTrigger()) {
    uint32_t held = now - _press_start_ms;
    Serial.printf("[PWR] Relache apres %lu ms\n", held);

    if (held >= 5000) {
      // ───── 5 s : arret complet ─────
      power_off();

    } else if (held >= 2000) {
      // ───── 2 s : veille ecran ─────
      enter_sleep();

    } else {
      // ───── Court : retour launcher ─────
      if (!_sleeping) {
        Serial.println("[PWR] Retour au carousel");
        bootloader_ui_return();
      } else {
        // Si on etait en veille, le reveil a deja ete fait dans enter_sleep()
        // on ne fait rien de plus
      }
    }
  }

  // IRQ batterie / chargeur (on lit tout pour vider le registre)
  pmu.clearIrqStatus();
}

// ── Batterie (pour status_bar) ────────────────────────────────────────────────
int pmu_get_battery_percent() {
  if (!_pmu_ok) return -1;
  if (!pmu.isBatteryConnect()) return -1;
  return (int)pmu.getBatteryPercent();
}
