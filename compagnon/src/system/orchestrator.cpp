#include "orchestrator.h"
#include "brain.h"
#include "../apps/meteo/meteo_app.h"
#include "../apps/bourse/bourse_app.h"
#include "../apps/radars/radar_app.h"
#include "../apps/musique/musique_app.h"
#include "../apps/nestor/nestor_app.h"
#include "../apps/smarthome/smarthome_app.h"
#include "../apps/ecovacs/ecovacs_app.h"

static ActiveApp _app = APP_LAUNCHER;

void orchestrator_init() {
    brain_init();
    Serial.println("[ORCH] OK");
}

void orchestrator_tick() {
    brain_tick();
    // Dispatch vers l'app active
    switch (_app) {
        case APP_METEO:     meteo_app_tick();     break;
        case APP_BOURSE:    bourse_app_tick();    break;
        case APP_RADAR:     radar_app_tick();     break;
        case APP_MUSIQUE:   musique_app_tick();   break;
        case APP_SMARTHOME: smarthome_app_tick(); break;
        case APP_ECOVACS:   ecovacs_app_tick();   break;
        case APP_NESTOR:    /* pas de tick périodique */ break;
        case APP_COMPANION: /* TODO: companion_app_tick() */ break;
        default: break;
    }
}

void orchestrator_set_app(ActiveApp a) {
    _app = a;
    Serial.printf("[ORCH] App: %d\n", a);
}

ActiveApp orchestrator_get_app() { return _app; }
