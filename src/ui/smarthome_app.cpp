#include "smarthome_app.h"
#include <Arduino.h>

// Device IDs — overrideable via config.h
#ifndef TUYA_SENSOR_ID
#define TUYA_SENSOR_ID  "YOUR_TUYA_SENSOR_DEVICE_ID"
#endif
#ifndef TUYA_LIGHT_ID
#define TUYA_LIGHT_ID   "YOUR_TUYA_LIGHT_DEVICE_ID"
#endif
#ifndef TUYA_PLUG_ID
#define TUYA_PLUG_ID    "YOUR_TUYA_PLUG_DEVICE_ID"
#endif

SmartHomeApp::SmartHomeApp(TuyaAPI* tuya)
    : _tuya(tuya), _root(nullptr) {}

void SmartHomeApp::create(lv_obj_t* parent) {
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(_root, 12, 0);
    lv_obj_set_style_gap(_root, 10, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // --- Titre ---
    lv_obj_t* title = lv_label_create(_root);
    lv_label_set_text(title, LV_SYMBOL_HOME " Maison");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // --- Capteurs temp/humidité ---
    lv_obj_t* sensor_card = lv_obj_create(_root);
    lv_obj_set_size(sensor_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sensor_card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sensor_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(sensor_card, 8, 0);
    lv_obj_set_style_radius(sensor_card, 10, 0);
    lv_obj_set_style_bg_color(sensor_card, lv_color_hex(0x1a3a4a), 0);

    _temp_label = lv_label_create(sensor_card);
    lv_label_set_text(_temp_label, LV_SYMBOL_CHARGE " --°C");

    _hum_label = lv_label_create(sensor_card);
    lv_label_set_text(_hum_label, "Hum: --%");

    // --- Lumière (Tuya) ---
    lv_obj_t* light_row = lv_obj_create(_root);
    lv_obj_set_size(light_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(light_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(light_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(light_row, 8, 0);
    lv_obj_clear_flag(light_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* light_label = lv_label_create(light_row);
    lv_label_set_text(light_label, LV_SYMBOL_EYE_OPEN " Lumière");

    _light_sw = lv_switch_create(light_row);
    lv_obj_add_event_cb(_light_sw, _onLightToggle, LV_EVENT_VALUE_CHANGED, this);

    // --- Luminosité ---
    lv_obj_t* bright_label = lv_label_create(_root);
    lv_label_set_text(bright_label, "Luminosité");

    _brightness_slider = lv_slider_create(_root);
    lv_obj_set_width(_brightness_slider, lv_pct(90));
    lv_slider_set_range(_brightness_slider, 10, 1000);
    lv_slider_set_value(_brightness_slider, 500, LV_ANIM_OFF);
    lv_obj_add_event_cb(_brightness_slider, _onBrightnessChange, LV_EVENT_RELEASED, this);

    // --- Prise Tuya ---
    lv_obj_t* plug_row = lv_obj_create(_root);
    lv_obj_set_size(plug_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(plug_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(plug_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(plug_row, 8, 0);
    lv_obj_clear_flag(plug_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* plug_label = lv_label_create(plug_row);
    lv_label_set_text(plug_label, LV_SYMBOL_CHARGE " Prise");

    _plug_sw = lv_switch_create(plug_row);
    lv_obj_add_event_cb(_plug_sw, _onPlugToggle, LV_EVENT_VALUE_CHANGED, this);

    // --- Status bar ---
    _status_label = lv_label_create(_root);
    lv_label_set_text(_status_label, "");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(_status_label, &lv_font_montserrat_10, 0);

    update();
}

void SmartHomeApp::destroy() {
    if (_root) {
        lv_obj_del(_root);
        _root = nullptr;
    }
}

void SmartHomeApp::update() {
    _refreshSensor();
    _refreshLight();
    _refreshPlug();
}

void SmartHomeApp::_refreshSensor() {
    if (!_tuya || !_tuya->isTokenValid()) return;
    float temp, hum;
    if (_tuya->getSensorData(TUYA_SENSOR_ID, temp, hum)) {
        char buf[32];
        snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " %.1f°C", temp);
        lv_label_set_text(_temp_label, buf);
        snprintf(buf, sizeof(buf), "Hum: %.0f%%", hum);
        lv_label_set_text(_hum_label, buf);
    }
}

void SmartHomeApp::_refreshLight() {
    if (!_tuya || !_tuya->isTokenValid()) return;
    TuyaDevice dev;
    if (_tuya->getDeviceStatus(TUYA_LIGHT_ID, dev)) {
        if (dev.is_on != (bool)lv_obj_has_state(_light_sw, LV_STATE_CHECKED)) {
            if (dev.is_on) lv_obj_add_state(_light_sw, LV_STATE_CHECKED);
            else           lv_obj_clear_state(_light_sw, LV_STATE_CHECKED);
        }
        lv_slider_set_value(_brightness_slider, dev.brightness, LV_ANIM_ON);
    }
}

void SmartHomeApp::_refreshPlug() {
    if (!_tuya || !_tuya->isTokenValid()) return;
    TuyaDevice dev;
    if (_tuya->getDeviceStatus(TUYA_PLUG_ID, dev)) {
        if (dev.is_on != (bool)lv_obj_has_state(_plug_sw, LV_STATE_CHECKED)) {
            if (dev.is_on) lv_obj_add_state(_plug_sw, LV_STATE_CHECKED);
            else           lv_obj_clear_state(_plug_sw, LV_STATE_CHECKED);
        }
    }
}

void SmartHomeApp::_setStatus(const char* msg) {
    if (_status_label) lv_label_set_text(_status_label, msg);
}

// --- Callbacks statiques ---

void SmartHomeApp::_onLightToggle(lv_event_t* e) {
    SmartHomeApp* self = (SmartHomeApp*)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(self->_light_sw, LV_STATE_CHECKED);
    self->_setStatus(on ? "Allumage..." : "Extinction...");
    if (self->_tuya && self->_tuya->isTokenValid()) {
        self->_tuya->switchDevice(TUYA_LIGHT_ID, on);
    }
    self->_setStatus("");
}

void SmartHomeApp::_onBrightnessChange(lv_event_t* e) {
    SmartHomeApp* self = (SmartHomeApp*)lv_event_get_user_data(e);
    int val = lv_slider_get_value(self->_brightness_slider);
    if (self->_tuya && self->_tuya->isTokenValid()) {
        self->_tuya->setBrightness(TUYA_LIGHT_ID, val);
    }
}

void SmartHomeApp::_onPlugToggle(lv_event_t* e) {
    SmartHomeApp* self = (SmartHomeApp*)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(self->_plug_sw, LV_STATE_CHECKED);
    self->_setStatus(on ? "Prise ON..." : "Prise OFF...");
    if (self->_tuya && self->_tuya->isTokenValid()) {
        self->_tuya->switchDevice(TUYA_PLUG_ID, on);
    }
    self->_setStatus("");
}
