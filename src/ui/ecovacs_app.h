#pragma once
#include <lvgl.h>
#include <Arduino.h>
#include "../api/ecovacs_api.h"

// ============================================================
// EcovacsApp — UI LVGL aspirateur Ecovacs
//
// Affiche : statut nettoyage, charge, batterie, zone nettoyée,
//           durée session, statut station.
// Commandes : Clean, Spot, Pause, Retour base.
// ============================================================

class EcovacsApp {
public:
    EcovacsApp(EcovacsAPI* api, const char* robotNickname);

    void create(lv_obj_t* parent);  // Créer l'UI LVGL
    void destroy();                 // Libérer
    void update();                  // Refresh état (appeler ~10s)

private:
    EcovacsAPI* _api;
    String      _nickname;
    lv_obj_t*   _root;

    // Labels d'état
    lv_obj_t* _status_label;   // Nettoyage: xxx
    lv_obj_t* _charge_label;   // Charge: xxx
    lv_obj_t* _station_label;  // Base: xxx
    lv_obj_t* _area_label;     // Zone: xx m²
    lv_obj_t* _time_label;     // Durée: xx min
    lv_obj_t* _feedback_label; // Retour d'action ("Démarrage…")

    // Batterie
    lv_obj_t* _battery_bar;
    lv_obj_t* _battery_label;

    // Boutons
    lv_obj_t* _btn_clean;
    lv_obj_t* _btn_spot;
    lv_obj_t* _btn_pause;
    lv_obj_t* _btn_charge;

    void _updateUI(const EcovacsState& state);
    void _setFeedback(const char* msg);

    static void _onClean(lv_event_t* e);
    static void _onSpot(lv_event_t* e);
    static void _onPause(lv_event_t* e);
    static void _onCharge(lv_event_t* e);
};
