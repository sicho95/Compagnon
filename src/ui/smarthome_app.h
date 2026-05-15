#pragma once
#include <lvgl.h>
#include <Arduino.h>
#include "../api/tuya_api.h"

// ============================================================
// SmartHomeApp — UI LVGL multi-devices
//
// Supporte N devices (lumières, prises, capteurs) via tableau
// DeviceEntry passé au constructeur. Les IDs viennent de
// config.h ou sont fournis dynamiquement.
// ============================================================

enum DeviceType {
    DEV_SENSOR,      // capteur temp/hum Tuya
    DEV_LIGHT,       // lumière Tuya (on/off + brightness + colortemp)
    DEV_PLUG,        // prise Tuya (on/off)
};

struct DeviceEntry {
    const char* id;    // Tuya device ID
    const char* name;  // Nom affiché
    DeviceType   type;
    bool hasBrightness; // lumière uniquement
    bool hasColorTemp;  // lumière uniquement
};

// État local d'un device
struct DeviceState {
    bool  on          = false;
    int   brightness  = 500;   // 10-1000
    int   colorTemp   = 2700;  // Kelvin
    float temp        = 0.f;
    float hum         = 0.f;
    bool  fetched     = false;
};

#define SMARTHOME_MAX_DEVICES 8

class SmartHomeApp {
public:
    // devices: pointeur vers tableau statique de DeviceEntry
    // count  : nombre d'entrées
    SmartHomeApp(TuyaAPI* tuya, const DeviceEntry* devices, int count);

    void create(lv_obj_t* parent);  // créer l'UI LVGL
    void destroy();                 // libérer
    void update();                  // refresh toutes les données (appeler ~30s)

private:
    TuyaAPI*           _tuya;
    const DeviceEntry* _devices;
    int                _count;
    DeviceState        _state[SMARTHOME_MAX_DEVICES];

    lv_obj_t* _root;
    lv_obj_t* _status_label;

    // Widgets par device (index == i)
    lv_obj_t* _sw[SMARTHOME_MAX_DEVICES];           // switch on/off
    lv_obj_t* _bright_sl[SMARTHOME_MAX_DEVICES];    // slider luminosité
    lv_obj_t* _ct_sl[SMARTHOME_MAX_DEVICES];        // slider color temp
    lv_obj_t* _temp_lbl[SMARTHOME_MAX_DEVICES];     // label temp
    lv_obj_t* _hum_lbl[SMARTHOME_MAX_DEVICES];      // label humidité

    void _buildSensorCard(lv_obj_t* parent, int i);
    void _buildLightCard(lv_obj_t* parent, int i);
    void _buildPlugCard(lv_obj_t* parent, int i);
    void _setStatus(const char* msg);
    void _refreshDevice(int i);

    // userData = packed: (this << 8) | index
    static void _onToggle(lv_event_t* e);
    static void _onBrightness(lv_event_t* e);
    static void _onColorTemp(lv_event_t* e);

    // Helpers pack/unpack user_data
    static void*  _pack(SmartHomeApp* app, int idx);
    static SmartHomeApp* _unpackApp(void* ud);
    static int           _unpackIdx(void* ud);
};
