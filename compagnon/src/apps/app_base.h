#pragma once
#include <Arduino.h>

// ─── IDs des apps ────────────────────────────────────────────────────────────
enum AppId : uint8_t {
    APP_NONE     = 0,
    APP_LAUNCHER = 1,
    APP_METEO    = 2,
    APP_BOURSE   = 3,
    APP_NESTOR   = 4,
    APP_MUSIQUE  = 5,
    APP_RADARS   = 6,
    APP_SMARTHOME= 7,
    APP_ECOVACS  = 8,
    APP_COUNT
};

// ─── Contrat minimal de chaque app ──────────────────────────────────────────
//
// Règles d'isolation mémoire (OBLIGATOIRES) :
//   1. Zéro buffer statique > 64 bytes dans un .cpp d'app
//   2. Tout l'état va dans une struct allouée dans start() / libérée dans stop()
//   3. Le seul static autorisé : un pointeur `static MyState *_s = nullptr`
//      → 4 bytes en RAM quand l'app est fermée
//   4. LVGL : lv_obj_del(_s->screen) dans stop() libère tout le sous-arbre
//   5. Tasks FreeRTOS : vTaskDelete(NULL) en fin de task réseau
//   6. SD : via sd_mgr pour tout ce qui dépasse 1KB de données persistantes
//
// Interface minimale :
//   void xxx_app_start();
//   void xxx_app_stop();
//   bool xxx_app_is_running();  // retourne _s != nullptr

struct AppDescriptor {
    AppId       id;
    const char *name;           // nom court (max 16 chars)
    const char *icon_symbol;    // symbole LVGL (ex: LV_SYMBOL_HOME)
    void (*start)();
    void (*stop)();
    bool (*is_running)();
};
