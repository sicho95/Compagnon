#pragma once
#include <Arduino.h>

typedef void (*PmuCallback)();

void hal_pmu_init();
void hal_pmu_tick();

int  hal_pmu_battery_pct();   // 0–100, -1 si PMIC indisponible
bool hal_pmu_is_charging();   // true si charge en cours (AXP2101)

void hal_pmu_screen_off();    // Éteint pixels (ESP reste actif)
void hal_pmu_screen_on();     // Rallume pixels + restaure MADCTL
void hal_pmu_enter_sleep();   // Light sleep + réveil sur AXP_INT
void hal_pmu_shutdown();      // Arrêt propre via AXP2101

// Callback appelé sur appui LONG bouton power (pour afficher menu UI)
void hal_pmu_set_long_press_cb(PmuCallback cb);
