/**
 * brain.h — Cerveau Nestor
 * ⚠️  SYNCHRONISATION OBLIGATOIRE
 *     Toute modification ici doit être répercutée dans :
 *     src/brain/ (PWA) et vice-versa.
 *
 * Contient la logique de raisonnement, mémoire court-terme,
 * et interface vers les APIs IA (via WiFi sur ESP32).
 */
#pragma once
#include <Arduino.h>

void brain_init();
void brain_tick();
const char* brain_process(const char* input);
