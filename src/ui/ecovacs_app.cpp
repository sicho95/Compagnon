#include "ecovacs_app.h"
#include <stdio.h>

// ── Palette AMOLED ────────────────────────────────────────────────────────────
#define COL_BG       0x0d1b2e
#define COL_CARD     0x112233
#define COL_CLEAN    0x1a3a1a
#define COL_PAUSE    0x885500
#define COL_CHARGE   0x1a5a2a
#define COL_SPOT     0x1a1a5a
#define COL_ACCENT   0x4fc3f7
#define COL_OK       0x81c784
#define COL_WARN     0xffcc44
#define COL_MUTED    0x888888

EcovacsApp::EcovacsApp(EcovacsAPI* api, const char* robotNickname)
    : _api(api), _nickname(robotNickname), _root(nullptr),
      _status_label(nullptr), _charge_label(nullptr), _station_label(nullptr),
      _area_label(nullptr), _time_label(nullptr), _feedback_label(nullptr),
      _battery_bar(nullptr), _battery_label(nullptr),
      _btn_clean(nullptr), _btn_spot(nullptr), _btn_pause(nullptr), _btn_charge(nullptr)
{}

void EcovacsApp::create(lv_obj_t* parent) {
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(_root, 12, 0);
    lv_obj_set_style_gap(_root, 8, 0);
    lv_obj_set_style_bg_color(_root, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(_root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // ── Titre ────────────────────────────────────────────────────────────────
    lv_obj_t* title = lv_label_create(_root);
    char t[64];
    snprintf(t, sizeof(t), LV_SYMBOL_REFRESH " %s", _nickname.c_str());
    lv_label_set_text(title, t);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_ACCENT), 0);

    // ── Carte état ───────────────────────────────────────────────────────────
    lv_obj_t* state_card = lv_obj_create(_root);
    lv_obj_set_size(state_card, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(state_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(state_card, 8, 0);
    lv_obj_set_style_gap(state_card, 4, 0);
    lv_obj_set_style_radius(state_card, 10, 0);
    lv_obj_set_style_bg_color(state_card, lv_color_hex(COL_CARD), 0);
    lv_obj_set_style_bg_opa(state_card, LV_OPA_COVER, 0);
    lv_obj_clear_flag(state_card, LV_OBJ_FLAG_SCROLLABLE);

    _status_label = lv_label_create(state_card);
    lv_label_set_text(_status_label, "Nettoyage: --");
    lv_obj_set_style_text_color(_status_label, lv_color_white(), 0);

    _charge_label = lv_label_create(state_card);
    lv_label_set_text(_charge_label, "Charge: --");
    lv_obj_set_style_text_color(_charge_label, lv_color_hex(COL_ACCENT), 0);

    _station_label = lv_label_create(state_card);
    lv_label_set_text(_station_label, "Base: --");
    lv_obj_set_style_text_color(_station_label, lv_color_hex(COL_OK), 0);

    // ── Batterie ─────────────────────────────────────────────────────────────
    lv_obj_t* bat_row = lv_obj_create(state_card);
    lv_obj_set_size(bat_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bat_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bat_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(bat_row, 2, 0);
    lv_obj_set_style_bg_opa(bat_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bat_row, 0, 0);
    lv_obj_clear_flag(bat_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bat_icon = lv_label_create(bat_row);
    lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_FULL " Batt.");
    lv_obj_set_style_text_color(bat_icon, lv_color_hex(COL_MUTED), 0);

    _battery_bar = lv_bar_create(bat_row);
    lv_obj_set_width(_battery_bar, lv_pct(50));
    lv_bar_set_range(_battery_bar, 0, 100);
    lv_bar_set_value(_battery_bar, 0, LV_ANIM_OFF);

    _battery_label = lv_label_create(bat_row);
    lv_label_set_text(_battery_label, "--%");
    lv_obj_set_style_text_color(_battery_label, lv_color_hex(COL_WARN), 0);

    // ── Zone + Durée ─────────────────────────────────────────────────────────
    lv_obj_t* stats_row = lv_obj_create(_root);
    lv_obj_set_size(stats_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(stats_row, 4, 0);
    lv_obj_set_style_bg_opa(stats_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(stats_row, 0, 0);
    lv_obj_clear_flag(stats_row, LV_OBJ_FLAG_SCROLLABLE);

    _area_label = lv_label_create(stats_row);
    lv_label_set_text(_area_label, "-- m\u00b2");
    lv_obj_set_style_text_color(_area_label, lv_color_hex(COL_MUTED), 0);

    _time_label = lv_label_create(stats_row);
    lv_label_set_text(_time_label, "-- min");
    lv_obj_set_style_text_color(_time_label, lv_color_hex(COL_MUTED), 0);

    // ── Boutons ──────────────────────────────────────────────────────────────
    lv_obj_t* btn_row = lv_obj_create(_root);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 4, 0);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    // Clean
    _btn_clean = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_clean, lv_color_hex(COL_CLEAN), 0);
    lv_obj_set_style_radius(_btn_clean, 8, 0);
    lv_label_set_text(lv_label_create(_btn_clean), LV_SYMBOL_PLAY " Clean");
    lv_obj_add_event_cb(_btn_clean, _onClean, LV_EVENT_CLICKED, this);

    // Spot
    _btn_spot = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_spot, lv_color_hex(COL_SPOT), 0);
    lv_obj_set_style_radius(_btn_spot, 8, 0);
    lv_label_set_text(lv_label_create(_btn_spot), "Spot");
    lv_obj_add_event_cb(_btn_spot, _onSpot, LV_EVENT_CLICKED, this);

    // Pause
    _btn_pause = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_pause, lv_color_hex(COL_PAUSE), 0);
    lv_obj_set_style_radius(_btn_pause, 8, 0);
    lv_label_set_text(lv_label_create(_btn_pause), LV_SYMBOL_PAUSE);
    lv_obj_add_event_cb(_btn_pause, _onPause, LV_EVENT_CLICKED, this);

    // Base
    _btn_charge = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_charge, lv_color_hex(COL_CHARGE), 0);
    lv_obj_set_style_radius(_btn_charge, 8, 0);
    lv_label_set_text(lv_label_create(_btn_charge), LV_SYMBOL_HOME " Base");
    lv_obj_add_event_cb(_btn_charge, _onCharge, LV_EVENT_CLICKED, this);

    // ── Feedback action ──────────────────────────────────────────────────────
    _feedback_label = lv_label_create(_root);
    lv_label_set_text(_feedback_label, "");
    lv_obj_set_style_text_color(_feedback_label, lv_color_hex(COL_WARN), 0);
    lv_obj_set_style_text_font(_feedback_label, &lv_font_montserrat_12, 0);

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

    snprintf(buf, sizeof(buf), "%d m\u00b2", state.cleanedArea);
    lv_label_set_text(_area_label, buf);

    snprintf(buf, sizeof(buf), "%d min", state.cleaningTime);
    lv_label_set_text(_time_label, buf);
}

void EcovacsApp::_setFeedback(const char* msg) {
    if (_feedback_label) lv_label_set_text(_feedback_label, msg);
}

// ── Callbacks ─────────────────────────────────────────────────────────────────
void EcovacsApp::_onClean(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_setFeedback("D\u00e9marrage nettoyage...");
    self->_api->startCleaning(self->_nickname.c_str());
    lv_label_set_text(self->_status_label, "Nettoyage: D\u00e9marrage...");
}

void EcovacsApp::_onSpot(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_setFeedback("Nettoyage ponctuel...");
    self->_api->startSpotCleaning(self->_nickname.c_str());
    lv_label_set_text(self->_status_label, "Nettoyage: Spot");
}

void EcovacsApp::_onPause(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_setFeedback("Pause...");
    self->_api->pauseCleaning(self->_nickname.c_str());
    lv_label_set_text(self->_status_label, "Nettoyage: Pause");
}

void EcovacsApp::_onCharge(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_setFeedback("Retour base...");
    self->_api->goCharge(self->_nickname.c_str());
    lv_label_set_text(self->_charge_label, "Charge: Retour base...");
}
