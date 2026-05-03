/**
 * NestorOS — Super-App Monolithique
 * Carte  : Waveshare ESP32-S3 Touch AMOLED 2.16" (480x480)
 * IDE    : Arduino IDE 2.x  +  ESP32 board package v3.3.5
 * Libs   : lvgl, Mylibrary (Waveshare), XPowersLib, SensorLib
 *
 * Point d'entrée unique. setup() initialise le hardware et
 * lance le launcher LVGL. loop() délègue tout à l'orchestrateur
 * et au timer LVGL.
 *
 * ─── Onglets de ce projet ────────────────────────────────────
 *  display_init.h / .cpp   → init écran AMOLED + touch
 *  bootloader_ui.h / .cpp  → menu graphique de sélection d'app
 *  nestor_app.h  / .cpp    → application Nestor (IA compagnon)
 *  radar_app.h   / .cpp    → app Radar (placeholder)
 *  lora_app.h    / .cpp    → app LoRa/GPS (placeholder)
 *  histoire_app.h/ .cpp    → app Histoire (placeholder)
 *  brain.h       / .cpp    → cerveau partagé (SYNC ↔ PWA src/brain/)
 *  orchestrator.h/ .cpp    → orchestrateur (SYNC ↔ PWA src/orchestrator/)
 * ─────────────────────────────────────────────────────────────
 */

#include <lvgl.h>
#include "display_init.h"
#include "bootloader_ui.h"
#include "orchestrator.h"

void setup() {
  Serial.begin(115200);
  display_init();           // Init écran AMOLED + LVGL + touch
  orchestrator_init();      // Init cerveau + état global
  bootloader_ui_show();     // Affiche le menu de sélection
}

void loop() {
  orchestrator_tick();      // Logique métier (toujours actif)
  lv_timer_handler();       // Rafraîchissement UI LVGL
  delay(5);
}
