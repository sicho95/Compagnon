#pragma once
#include <lvgl.h>
#include "../api/tuya_api.h"
#include "../api/sinricpro_bridge.h"

// ============================================================
// SmartHome App — UI LVGL
// Affiche :
//  - Capteurs temp/humidité (Tuya)
//  - Contrôle lumières (Tuya)
//  - Contrôle prises Alexa (SinricPro)
// ============================================================

class SmartHomeApp {
public:
    SmartHomeApp(TuyaAPI* tuya, SinricProBridge* sinric);

    void        create(lv_obj_t* parent);   // créer l'UI
    void        destroy();                  // libérer l'UI
    void        update();                   // refresh données (appeler toutes les 30s)

private:
    TuyaAPI*            _tuya;
    SinricProBridge*    _sinric;

    lv_obj_t*           _root;
    lv_obj_t*           _temp_label;
    lv_obj_t*           _hum_label;
    lv_obj_t*           _light_sw;
    lv_obj_t*           _brightness_slider;
    lv_obj_t*           _plug_sw;
    lv_obj_t*           _status_label;

    // IDs devices — à configurer dans config.h
    // TUYA_SENSOR_ID, TUYA_LIGHT_ID, SINRIC_PLUG_ID

    static void _onLightToggle(lv_event_t* e);
    static void _onBrightnessChange(lv_event_t* e);
    static void _onPlugToggle(lv_event_t* e);

    void        _setStatus(const char* msg);
    void        _refreshSensor();
    void        _refreshLight();
};
