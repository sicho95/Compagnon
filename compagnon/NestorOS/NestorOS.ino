/**
 * NestorOS.ino - Compagnon v3
 * Carte  : Waveshare ESP32-S3 Touch AMOLED 2.16" (480x480)
 * IDE    : Arduino IDE 2.x + ESP32 board package v3.3.5+
 * Libs   : lvgl, Mylibrary (Waveshare), XPowersLib, SensorLib,
 *          WiFiManager (tzapu), BluetoothSerial (built-in)
 *
 * Fix v4 :
 *  - Bluetooth DESACTIVE (trop gourmand en RAM avec WiFi+LVGL)
 *  - WiFiManager initialisé EN DERNIER, après display+LVGL+carousel
 *  - lv_timer_handler() appelé pendant wifi_manager_init() via callback
 *    pour que l'écran reste actif pendant la connexion WiFi
 */

#include <lvgl.h>
#include "display_init.h"
#include "pwr_button.h"
#include "status_bar.h"
#include "wifi_manager_ui.h"
#include "bootloader_ui.h"
#include "orchestrator.h"

// BT désactivé temporairement pour libérer ~60KB de RAM
// #include "bt_manager.h"

// ── Boutons physiques GPIO ───────────────────────────────────────────────────
#define BTN_PREV_PIN    0    // BOOT button - active-low
#define BTN_NEXT_PIN   18   // Bouton custom - active-low
#define BTN_DEBOUNCE_MS 50

static bool     prev_btn_last = true;
static bool     next_btn_last = true;
static uint32_t prev_last_ms  = 0;
static uint32_t next_last_ms  = 0;

static void buttons_init() {
  pinMode(BTN_PREV_PIN, INPUT_PULLUP);
  pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
}

static void buttons_tick() {
  uint32_t now = millis();
  bool prev_now = digitalRead(BTN_PREV_PIN);
  bool next_now = digitalRead(BTN_NEXT_PIN);

  if (!prev_now && prev_btn_last && (now - prev_last_ms) > BTN_DEBOUNCE_MS) {
    prev_last_ms = now;
    Serial.println("[BTN] PREV");
    bootloader_ui_prev();
  }
  if (!next_now && next_btn_last && (now - next_last_ms) > BTN_DEBOUNCE_MS) {
    next_last_ms = now;
    Serial.println("[BTN] NEXT");
    bootloader_ui_next();
  }

  prev_btn_last = prev_now;
  next_btn_last = next_now;
}

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[COMPAGNON] Demarrage v3...");
  Serial.printf("[MEM] Heap libre: %lu octets\n", (unsigned long)ESP.getFreeHeap());
  Serial.printf("[MEM] PSRAM libre: %lu octets\n", (unsigned long)ESP.getFreePsram());

  // ── 1. Ecran en premier — indispensable pour voir quoi que ce soit ──
  display_init();         // Ecran AMOLED + LVGL + touch

  // ── 2. Carousel visible immédiatement ──────────────────────────────
  orchestrator_init();
  bootloader_ui_show();   // Lance le carousel - l'ecran doit s'allumer ici

  // Quelques frames LVGL pour que l'écran s'affiche avant d'init le WiFi
  for (int i = 0; i < 20; i++) {
    lv_timer_handler();
    delay(10);
  }

  // ── 3. Status bar ──────────────────────────────────────────────────
  status_bar_init();

  // ── 4. PWR button ───────────────────────────────────────────────────
  pwr_button_init();
  buttons_init();

  // ── 5. WiFi EN DERNIER (gros consommateur de heap) ──────────────────
  Serial.printf("[MEM] Heap avant WiFi: %lu octets\n", (unsigned long)ESP.getFreeHeap());
  wifi_manager_init();

  // BT desactive - trop gourmand en RAM simultanement avec WiFi+LVGL
  // bt_manager_init();

  Serial.println("[COMPAGNON] Pret.");
  Serial.printf("[MEM] Heap final: %lu octets\n", (unsigned long)ESP.getFreeHeap());
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
  pwr_button_tick();
  buttons_tick();
  // bt_manager_tick();    // desactive
  status_bar_tick();
  orchestrator_tick();
  lv_timer_handler();
  delay(5);
}
