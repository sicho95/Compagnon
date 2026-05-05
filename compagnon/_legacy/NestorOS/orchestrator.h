/**
 * orchestrator.h — Orchestrateur NestorOS
 * ⚠️  SYNCHRONISATION OBLIGATOIRE
 *     Toute modification ici doit être répercutée dans :
 *     src/orchestrator/ (PWA) et vice-versa.
 *
 * Gère l'état global de l'application, coordonne le cerveau,
 * les apps et les événements système (batterie, WiFi, etc.).
 */
#pragma once
#include <Arduino.h>

typedef enum {
  APP_LAUNCHER = 0,
  APP_NESTOR,
  APP_RADAR,
  APP_LORA,
  APP_HISTOIRE
} ActiveApp;

void orchestrator_init();
void orchestrator_tick();
void orchestrator_set_app(ActiveApp app);
ActiveApp orchestrator_get_app();
