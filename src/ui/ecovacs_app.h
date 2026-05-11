#pragma once
#include <lvgl.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
// Ecovacs X8 Pro Omni — App UI LVGL
// API: api-gl.ecovacs.com (API REST non-officielle)
//
// Setup:
//  1. Remplir ECOVACS_EMAIL + ECOVACS_PASSWORD_HASH dans config.h
//     (md5 lowercase du mot de passe)
//  2. Remplir ECOVACS_COUNTRY ("fr"), ECOVACS_CONTINENT ("eu")
// ============================================================

enum class EcovacsStatus {
    IDLE,
    CLEANING,
    RETURNING,
    CHARGING,
    ERROR,
    UNKNOWN
};

struct EcovacsState {
    EcovacsStatus   status;
    int             battery;     // %
    String          cleanMode;   // auto, area, room...
    int             cleanedArea; // m²
    int             cleanedTime; // secondes
};

class EcovacsApp {
public:
    EcovacsApp();

    void        create(lv_obj_t* parent);
    void        destroy();
    void        update();   // appeler toutes les 30s

private:
    lv_obj_t*   _root;
    lv_obj_t*   _status_label;
    lv_obj_t*   _battery_bar;
    lv_obj_t*   _battery_label;
    lv_obj_t*   _area_label;
    lv_obj_t*   _btn_clean;
    lv_obj_t*   _btn_return;
    lv_obj_t*   _btn_pause;
    lv_obj_t*   _mode_label;

    String      _authToken;
    String      _deviceId;
    bool        _authenticated;

    bool        _login();
    bool        _getDeviceState(EcovacsState& out);
    bool        _sendCleanCommand(const char* command, const char* args = "{}");

    static void _onClean(lv_event_t* e);
    static void _onReturn(lv_event_t* e);
    static void _onPause(lv_event_t* e);

    void        _updateUI(const EcovacsState& state);
    static const char* _statusStr(EcovacsStatus s);
};
