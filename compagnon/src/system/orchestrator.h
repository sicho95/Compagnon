#pragma once
#include <Arduino.h>

typedef enum {
    APP_LAUNCHER = 0,
    APP_NESTOR,
    APP_RADAR,
    APP_BOURSE,
    APP_METEO,
    APP_MUSIQUE,
    APP_COMPANION
} ActiveApp;

void orchestrator_init();
void orchestrator_tick();
void orchestrator_set_app(ActiveApp app);
ActiveApp orchestrator_get_app();
