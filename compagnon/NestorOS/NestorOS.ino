/**
 * NestorOS.ino - Compagnon v3
 * Carte  : Waveshare ESP32-S3 Touch AMOLED 2.16" (480x480)
 * IDE    : Arduino IDE 2.x + ESP32 board package v3.3.5+
 * Libs   : lvgl, Mylibrary (Waveshare), XPowersLib, SensorLib,
 *          WiFiManager (tzapu), BluetoothSerial (built-in)
 *
 * ─── Onglets ─────────────────────────────────────────────────────────────
 *  display_init        -> init ecran AMOLED + touch
 *  pwr_button          -> bouton PWR AXP2101 (court/2s/5s)
 *  status_bar          -> barre d'etat persistante (heure, batt, WiFi, BT)
 *  wifi_manager_ui     -> captive portal Compagnon_Setup + QR code
 *  bt_manager          -> Bluetooth Classic manager
 *  bootloader_ui       -> carousel apps (swipe + boutons GPIO)
 *  nestor_app          -> Nestor IA (PWA via WiFi)
 *  radar_app           -> placeholder Radar RF
 *  lora_app            -> placeholder LoRa/GPS
 *  histoire_app        -> placeholder Histoires
 *  brain / orchestrator -> logique partagee
 * ──────────────────────────────────────────────────────────────────────────
 *
 * Boutons physiques :
 *  GPIO0  (BOOT)  -> carte precedente dans le carousel
 *  GPIO18 (BTN2)  -> carte suivante dans le carousel
 *  PWR    (AXP2101, GPIO13 IRQ) :
 *    Appui court  -> retour au carousel
 *    Maintien 2s  -> veille ecran
 *    Maintien 5s  -> arret complet
 */

#include <lvgl.h>
#include "display_init.h"
#include "pwr_button.h"
#include "status_bar.h"
#include "wifi_manager_ui.h"
#include "bt_manager.h"
#include "bootloader_ui.h"
#include "orchestrator.h"

// ── Boutons physiques GPIO ───────────────────────────────────────────────────
#define BTN_PREV_PIN  0    // BOOT button - active-low
#define BTN_NEXT_PIN  18   // Bouton custom - active-low

static bool prev_btn_last = true;
static bool next_btn_last = true;

static void buttons_init() {
  pinMode(BTN_PREV_PIN, INPUT_PULLUP);
  pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
}

static void buttons_tick() {
  bool prev_now = digitalRead(BTN_PREV_PIN);
  bool next_now = digitalRead(BTN_NEXT_PIN);
  if (!prev_now && prev_btn_last) bootloader_ui_prev();
  if (!next_now && next_btn_last) bootloader_ui_next();
  prev_btn_last = prev_now;
  next_btn_last = next_now;
}

// ── setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[COMPAGNON] Demarrage v3...");

  display_init();         // Ecran AMOLED + LVGL + touch (Wire.begin inclus)
  pwr_button_init();      // AXP2101 + IRQ bouton PWR
  status_bar_init();      // Barre d'etat layer systeme LVGL
  buttons_init();         // GPIO BOOT (0) et BTN2 (18)
  bt_manager_init();      // Bluetooth Classic "Compagnon"

  // WiFi - captive portal si pas de credentials en NVS
  wifi_manager_init();

  orchestrator_init();
  bootloader_ui_show();   // Lance le carousel

  Serial.println("[COMPAGNON] Pret.");
}

// ── loop ─────────────────────────────────────────────────────────────────────
void loop() {
  pwr_button_tick();      // Gestion PWR (court/2s/5s)
  buttons_tick();         // GPIO BOOT + BTN2
  bt_manager_tick();      // Messages BT entrants
  status_bar_tick();      // Heure/batt/icones toutes 5s
  orchestrator_tick();    // Logique metier
  lv_timer_handler();     // Rendu LVGL
  delay(5);
}
