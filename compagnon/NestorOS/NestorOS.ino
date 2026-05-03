/**
 * NestorOS.ino — Compagnon v2
 * Carte  : Waveshare ESP32-S3 Touch AMOLED 2.16" (480×480)
 * IDE    : Arduino IDE 2.x + ESP32 board package v3.3.5+
 * Libs   : lvgl, Mylibrary (Waveshare), XPowersLib, SensorLib,
 *          WiFiManager (tzapu), BluetoothSerial (built-in)
 *
 * ─── Onglets ────────────────────────────────────────────────────
 *  display_init        → init écran AMOLED + touch
 *  status_bar          → barre d'état persistante (heure, batt, WiFi, BT)
 *  wifi_manager_ui     → captive portal Compagnon_Setup + QR code
 *  bt_manager          → Bluetooth Classic manager
 *  bootloader_ui       → carousel apps (swipe + boutons GPIO)
 *  nestor_app          → Nestor IA (PWA via WiFi)
 *  radar_app           → placeholder Radar RF
 *  lora_app            → placeholder LoRa/GPS
 *  histoire_app        → placeholder Histoires
 *  brain / orchestrator → logique partagée
 * ─────────────────────────────────────────────────────────────────
 *
 * Boutons physiques :
 *  GPIO0  (BOOT)  → carte précédente dans le carousel
 *  GPIO18 (BTN2)  → carte suivante dans le carousel
 */

#include <lvgl.h>
#include "display_init.h"
#include "status_bar.h"
#include "wifi_manager_ui.h"
#include "bt_manager.h"
#include "bootloader_ui.h"
#include "orchestrator.h"

// ── Boutons physiques ──────────────────────────────────────────
#define BTN_PREV_PIN  0   // BOOT button — active-low
#define BTN_NEXT_PIN  18  // Bouton custom — active-low

static bool prev_btn_last = true;
static bool next_btn_last = true;

static void buttons_init() {
  pinMode(BTN_PREV_PIN, INPUT_PULLUP);
  pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
}

// Lecture avec anti-rebond simple (niveau bas = appuyé)
static void buttons_tick() {
  bool prev_now = digitalRead(BTN_PREV_PIN);
  bool next_now = digitalRead(BTN_NEXT_PIN);

  if (!prev_now && prev_btn_last) {   // front descendant
    bootloader_ui_prev();
  }
  if (!next_now && next_btn_last) {   // front descendant
    bootloader_ui_next();
  }

  prev_btn_last = prev_now;
  next_btn_last = next_now;
}

// ── setup ──────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[COMPAGNON] Demarrage...");

  display_init();           // Écran AMOLED + LVGL + touch
  status_bar_init();        // Barre d'état (layer système LVGL)
  buttons_init();           // GPIO boutons physiques
  bt_manager_init();        // Bluetooth Classic

  // WiFi — captive portal si pas de credentials en NVS
  // Bloquant jusqu'à connexion OU jusqu'à ce que l'utilisateur
  // configure via le portail Compagnon_Setup.
  wifi_manager_init();

  orchestrator_init();
  bootloader_ui_show();     // Lance le carousel

  Serial.println("[COMPAGNON] Pret.");
}

// ── loop ───────────────────────────────────────────────────────
void loop() {
  buttons_tick();           // Lecture boutons physiques
  bt_manager_tick();        // Messages BT entrants
  status_bar_tick();        // Rafraîchit heure/batt/icônes (toutes 5s)
  orchestrator_tick();      // Logique métier
  lv_timer_handler();       // Rafraîchissement UI LVGL
  delay(5);
}
