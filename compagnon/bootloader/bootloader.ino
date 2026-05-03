/**
 * Nestor Bootloader Graphique
 * Carte : Waveshare ESP32-S3 Touch AMOLED 2.16"
 * Framework : Arduino + LVGL
 *
 * Affiche un menu de sélection d'application au démarrage.
 * Les apps disponibles sont définies dans app_registry[].
 */

#include <lvgl.h>
#include "display_init.h"
#include "app_registry.h"

// ── Entrées du menu ──────────────────────────────────────────────────────────
const AppEntry apps[] = {
  { "Nestor",         "Compagnon IA",      LV_SYMBOL_CALL,    app_launch_nestor  },
  { "Radar",          "Scan RF",           LV_SYMBOL_WIFI,    app_launch_placeholder },
  { "LoRa Tracker",   "GPS + LoRa",        LV_SYMBOL_GPS,     app_launch_placeholder },
  { "Histoire",       "Lecture IA",        LV_SYMBOL_FILE,    app_launch_placeholder },
};
const uint8_t APP_COUNT = sizeof(apps) / sizeof(apps[0]);

void setup() {
  Serial.begin(115200);
  display_init();         // Init écran AMOLED + LVGL
  bootloader_ui_create(apps, APP_COUNT);
}

void loop() {
  lv_timer_handler();     // Boucle LVGL
  delay(5);
}
