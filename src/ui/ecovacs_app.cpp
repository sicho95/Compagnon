#include "ecovacs_app.h"
#include <Arduino.h>

EcovacsApp::EcovacsApp(EcovacsAPI* api, const char* robotNickname)
    : _api(api), _nickname(robotNickname), _root(nullptr) {}

void EcovacsApp::create(lv_obj_t* parent) {
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(_root, 12, 0);
    lv_obj_set_style_gap(_root, 8, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // Titre avec nom du robot
    lv_obj_t* title = lv_label_create(_root);
    char t[64]; snprintf(t, sizeof(t), LV_SYMBOL_REFRESH " %s", _nickname.c_str());
    lv_label_set_text(title, t);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // --- Nettoyage status ---
    _status_label = lv_label_create(_root);
    lv_label_set_text(_status_label, "Nettoyage: --");

    // --- Charge status ---
    _charge_label = lv_label_create(_root);
    lv_label_set_text(_charge_label, "Charge: --");
    lv_obj_set_style_text_color(_charge_label, lv_color_hex(0x4fc3f7), 0);

    // --- Station status ---
    _station_label = lv_label_create(_root);
    lv_label_set_text(_station_label, "Base: --");
    lv_obj_set_style_text_color(_station_label, lv_color_hex(0x81c784), 0);

    // --- Batterie ---
    lv_obj_t* bat_row = lv_obj_create(_root);
    lv_obj_set_size(bat_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bat_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bat_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(bat_row, 4, 0);
    lv_obj_clear_flag(bat_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bat_icon = lv_label_create(bat_row);
    lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_FULL " Batt.");

    _battery_bar = lv_bar_create(bat_row);
    lv_obj_set_width(_battery_bar, lv_pct(55));
    lv_bar_set_range(_battery_bar, 0, 100);
    lv_bar_set_value(_battery_bar, 0, LV_ANIM_OFF);

    _battery_label = lv_label_create(bat_row);
    lv_label_set_text(_battery_label, "--%");

    // --- Zone ---
    _area_label = lv_label_create(_root);
    lv_label_set_text(_area_label, "Zone: -- m\u00b2");
    lv_obj_set_style_text_color(_area_label, lv_color_hex(0x888888), 0);

    // --- Boutons ---
    lv_obj_t* btn_row = lv_obj_create(_root);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 4, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    _btn_clean = lv_btn_create(btn_row);
    lv_label_set_text(lv_label_create(_btn_clean), LV_SYMBOL_PLAY " Clean");
    lv_obj_add_event_cb(_btn_clean, _onClean, LV_EVENT_CLICKED, this);

    _btn_pause = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_pause, lv_color_hex(0x885500), 0);
    lv_label_set_text(lv_label_create(_btn_pause), LV_SYMBOL_PAUSE);
    lv_obj_add_event_cb(_btn_pause, _onPause, LV_EVENT_CLICKED, this);

    _btn_charge = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_charge, lv_color_hex(0x1a5a2a), 0);
    lv_label_set_text(lv_label_create(_btn_charge), LV_SYMBOL_HOME " Base");
    lv_obj_add_event_cb(_btn_charge, _onCharge, LV_EVENT_CLICKED, this);

    update();
}

void EcovacsApp::destroy() {
    if (_root) { lv_obj_del(_root); _root = nullptr; }
}

void EcovacsApp::update() {
    EcovacsState state;
    if (_api && _api->getState(_nickname.c_str(), state)) {
        _updateUI(state);
    }
}

void EcovacsApp::_updateUI(const EcovacsState& state) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Nettoyage: %s", EcovacsAPI::cleanStFr(state.cleanSt));
    lv_label_set_text(_status_label, buf);

    snprintf(buf, sizeof(buf), "Charge: %s", EcovacsAPI::chargeStFr(state.chargeSt));
    lv_label_set_text(_charge_label, buf);

    snprintf(buf, sizeof(buf), "Base: %s", EcovacsAPI::stationStFr(state.stationSt));
    lv_label_set_text(_station_label, buf);

    lv_bar_set_value(_battery_bar, state.battery, LV_ANIM_ON);
    snprintf(buf, sizeof(buf), "%d%%", state.battery);
    lv_label_set_text(_battery_label, buf);

    snprintf(buf, sizeof(buf), "Zone: %d m\u00b2", state.cleanedArea);
    lv_label_set_text(_area_label, buf);
}

void EcovacsApp::_onClean(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_api->startCleaning(self->_nickname.c_str());
    lv_label_set_text(self->_status_label, "Nettoyage: Démarrage...");
}
void EcovacsApp::_onPause(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_api->pauseCleaning(self->_nickname.c_str());
    lv_label_set_text(self->_status_label, "Nettoyage: Pause");
}
void EcovacsApp::_onCharge(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_api->goCharge(self->_nickname.c_str());
    lv_label_set_text(self->_charge_label, "Charge: Retour base...");
}
