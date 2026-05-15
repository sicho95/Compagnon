#include "app_registry.h"

// Inclure les headers de chaque app
#include "meteo/meteo_app.h"
#include "bourse/bourse_app.h"
#include "nestor/nestor_app.h"
#include "musique/musique_app.h"
#include "radars/radars_app.h"
#include "smarthome/smarthome_app.h"
#include "ecovacs/ecovacs_app.h"

// ─── Table des apps ───────────────────────────────────────────────────────────
static const AppDescriptor _apps[] = {
    {
        APP_METEO,     "Météo",      LV_SYMBOL_UP,
        meteo_app_start,    meteo_app_stop,    meteo_app_is_running
    },
    {
        APP_BOURSE,    "Bourse",     LV_SYMBOL_CHARGE,
        bourse_app_start,   bourse_app_stop,   bourse_app_is_running
    },
    {
        APP_NESTOR,    "Nestor",     LV_SYMBOL_COMMENT,
        nestor_app_start,   nestor_app_stop,   nestor_app_is_running
    },
    {
        APP_MUSIQUE,   "Musique",    LV_SYMBOL_AUDIO,
        musique_app_start,  musique_app_stop,  musique_app_is_running
    },
    {
        APP_RADARS,    "Radars",     LV_SYMBOL_DRIVE,
        radars_app_start,   radars_app_stop,   radars_app_is_running
    },
    {
        APP_SMARTHOME, "Maison",     LV_SYMBOL_HOME,
        smarthome_app_start, smarthome_app_stop, smarthome_app_is_running
    },
    {
        APP_ECOVACS,   "Aspirateur", LV_SYMBOL_REFRESH,
        ecovacs_app_start,  ecovacs_app_stop,  ecovacs_app_is_running
    },
};

static constexpr int APP_COUNT_REGISTERED =
    sizeof(_apps) / sizeof(_apps[0]);

void app_registry_init() {
    Serial.printf("[APP_REG] %d apps enregistrées\n", APP_COUNT_REGISTERED);
}

const AppDescriptor* app_registry_get(AppId id) {
    for (int i = 0; i < APP_COUNT_REGISTERED; i++) {
        if (_apps[i].id == id) return &_apps[i];
    }
    return nullptr;
}

void app_registry_stop_all() {
    for (int i = 0; i < APP_COUNT_REGISTERED; i++) {
        if (_apps[i].is_running && _apps[i].is_running()) {
            Serial.printf("[APP_REG] Stop: %s\n", _apps[i].name);
            _apps[i].stop();
        }
    }
}

int app_registry_count() {
    return APP_COUNT_REGISTERED;
}

const AppDescriptor* app_registry_get_all() {
    return _apps;
}
