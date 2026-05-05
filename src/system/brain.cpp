#include "brain.h"
#include "orchestrator.h"
#include "../apps/meteo/meteo_app.h"
#include <Arduino.h>

void brain_init()  { Serial.println("[BRAIN] OK"); }

void brain_tick() {
    switch (orchestrator_get_app()) {
        case APP_METEO:  meteo_app_tick();  break;
        default: break;
    }
}

const char* brain_process(const char*) { return "[brain] stub"; }
