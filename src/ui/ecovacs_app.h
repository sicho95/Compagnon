#pragma once
#include <lvgl.h>
#include "../api/ecovacs_api.h"

// ============================================================
// Ecovacs App — UI LVGL pour X8 Pro Omni
// Utilise EcovacsAPI (open.ecovacs.com)
// ============================================================

class EcovacsApp {
public:
    EcovacsApp(EcovacsAPI* api, const char* robotNickname);

    void create(lv_obj_t* parent);
    void destroy();
    void update();  // appeler toutes les 30s

private:
    EcovacsAPI* _api;
    String      _nickname;

    lv_obj_t*   _root;
    lv_obj_t*   _status_label;
    lv_obj_t*   _charge_label;
    lv_obj_t*   _station_label;
    lv_obj_t*   _battery_bar;
    lv_obj_t*   _battery_label;
    lv_obj_t*   _area_label;
    lv_obj_t*   _btn_clean;
    lv_obj_t*   _btn_pause;
    lv_obj_t*   _btn_charge;

    static void _onClean(lv_event_t* e);
    static void _onPause(lv_event_t* e);
    static void _onCharge(lv_event_t* e);

    void _updateUI(const EcovacsState& state);
};
