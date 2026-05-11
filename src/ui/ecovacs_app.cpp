#include "ecovacs_app.h"
#include <WiFiClientSecure.h>
#include <mbedtls/md.h>

#ifndef ECOVACS_EMAIL
#define ECOVACS_EMAIL          "your@email.com"
#endif
#ifndef ECOVACS_PASSWORD_HASH
#define ECOVACS_PASSWORD_HASH  "md5_of_your_password"  // echo -n 'password' | md5sum
#endif
#ifndef ECOVACS_COUNTRY
#define ECOVACS_COUNTRY        "fr"
#endif
#ifndef ECOVACS_CONTINENT
#define ECOVACS_CONTINENT      "eu"
#endif

#define ECOVACS_AUTH_URL   "https://gl-" ECOVACS_CONTINENT "-api.ecovacs.com/v1/private/" ECOVACS_COUNTRY "/en/0/common/checkAppSignWithDid"
#define ECOVACS_API_URL    "https://portal-" ECOVACS_CONTINENT ".ecouser.net:8007/api/"

EcovacsApp::EcovacsApp() : _root(nullptr), _authenticated(false) {}

void EcovacsApp::create(lv_obj_t* parent) {
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));
    lv_obj_set_flex_flow(_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(_root, 12, 0);
    lv_obj_set_style_gap(_root, 10, 0);
    lv_obj_clear_flag(_root, LV_OBJ_FLAG_SCROLLABLE);

    // Titre
    lv_obj_t* title = lv_label_create(_root);
    lv_label_set_text(title, "🧹 X8 Pro Omni");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);

    // Status
    _status_label = lv_label_create(_root);
    lv_label_set_text(_status_label, "Connexion...");
    lv_obj_set_style_text_color(_status_label, lv_color_hex(0x4fc3f7), 0);

    // Batterie
    lv_obj_t* bat_row = lv_obj_create(_root);
    lv_obj_set_size(bat_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bat_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bat_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(bat_row, 4, 0);
    lv_obj_clear_flag(bat_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bat_icon = lv_label_create(bat_row);
    lv_label_set_text(bat_icon, LV_SYMBOL_BATTERY_FULL);

    _battery_bar = lv_bar_create(bat_row);
    lv_obj_set_width(_battery_bar, lv_pct(65));
    lv_bar_set_range(_battery_bar, 0, 100);
    lv_bar_set_value(_battery_bar, 0, LV_ANIM_OFF);

    _battery_label = lv_label_create(bat_row);
    lv_label_set_text(_battery_label, "--%%");

    // Zone nettoyée
    _area_label = lv_label_create(_root);
    lv_label_set_text(_area_label, "Zone: -- m²");
    lv_obj_set_style_text_color(_area_label, lv_color_hex(0x888888), 0);

    // Mode
    _mode_label = lv_label_create(_root);
    lv_label_set_text(_mode_label, "Mode: --");

    // Boutons
    lv_obj_t* btn_row = lv_obj_create(_root);
    lv_obj_set_size(btn_row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_row, 4, 0);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    _btn_clean = lv_btn_create(btn_row);
    lv_obj_t* lbl_clean = lv_label_create(_btn_clean);
    lv_label_set_text(lbl_clean, LV_SYMBOL_PLAY " Clean");
    lv_obj_add_event_cb(_btn_clean, _onClean, LV_EVENT_CLICKED, this);

    _btn_pause = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_pause, lv_color_hex(0x885500), 0);
    lv_obj_t* lbl_pause = lv_label_create(_btn_pause);
    lv_label_set_text(lbl_pause, LV_SYMBOL_PAUSE);
    lv_obj_add_event_cb(_btn_pause, _onPause, LV_EVENT_CLICKED, this);

    _btn_return = lv_btn_create(btn_row);
    lv_obj_set_style_bg_color(_btn_return, lv_color_hex(0x1a5a2a), 0);
    lv_obj_t* lbl_ret = lv_label_create(_btn_return);
    lv_label_set_text(lbl_ret, LV_SYMBOL_HOME " Base");
    lv_obj_add_event_cb(_btn_return, _onReturn, LV_EVENT_CLICKED, this);

    // Première connexion + refresh
    if (_login()) update();
}

void EcovacsApp::destroy() {
    if (_root) { lv_obj_del(_root); _root = nullptr; }
}

void EcovacsApp::update() {
    EcovacsState state;
    if (_getDeviceState(state)) _updateUI(state);
}

// ---- Login (simplifié, token long-durée) ----
bool EcovacsApp::_login() {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    // Ecovacs utilise un auth multi-étapes — ici on utilise le flow simplifié
    // Pour une implémentation complète, voir: github.com/sucks/ozmo
    String url = String(ECOVACS_AUTH_URL);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "account=" + String(ECOVACS_EMAIL)
                + "&password=" + String(ECOVACS_PASSWORD_HASH)
                + "&requestId=" + String(millis())
                + "&authTimespan=" + String(millis())
                + "&country=" + String(ECOVACS_COUNTRY);

    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("[Ecovacs] Login failed: %d\n", code);
        http.end();
        if (_status_label) lv_label_set_text(_status_label, "Auth échouée");
        return false;
    }
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    http.end();

    if (doc["code"].as<int>() == 0) {
        _authToken = doc["data"]["accessToken"].as<String>();
        _deviceId  = doc["data"]["uid"].as<String>();
        _authenticated = true;
        Serial.println("[Ecovacs] Logged in");
        return true;
    }
    if (_status_label) lv_label_set_text(_status_label, "Auth échouée");
    return false;
}

// ---- Get device state ----
bool EcovacsApp::_getDeviceState(EcovacsState& out) {
    if (!_authenticated) return false;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(ECOVACS_API_URL) + "appsvr/app.do";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument body;
    body["todo"] = "GetCleanState";
    body["did"]  = _deviceId;
    body["token"] = _authToken;
    String bodyStr;
    serializeJson(body, bodyStr);

    int code = http.POST(bodyStr);
    if (code != 200) { http.end(); return false; }
    JsonDocument resp;
    deserializeJson(resp, http.getString());
    http.end();

    out.battery     = resp["battery"].as<int>();
    out.cleanedArea = resp["cleanedArea"].as<int>();
    out.cleanedTime = resp["cleanedTime"].as<int>();
    out.cleanMode   = resp["cleanMode"].as<String>();

    String st = resp["status"].as<String>();
    if      (st == "idle")     out.status = EcovacsStatus::IDLE;
    else if (st == "auto")     out.status = EcovacsStatus::CLEANING;
    else if (st == "returning")out.status = EcovacsStatus::RETURNING;
    else if (st == "charging") out.status = EcovacsStatus::CHARGING;
    else if (st == "error")    out.status = EcovacsStatus::ERROR;
    else                       out.status = EcovacsStatus::UNKNOWN;

    return true;
}

// ---- Send command ----
bool EcovacsApp::_sendCleanCommand(const char* command, const char* args) {
    if (!_authenticated) return false;
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(ECOVACS_API_URL) + "appsvr/app.do";
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");

    JsonDocument body;
    body["todo"]    = "sendCleanCmd";
    body["did"]     = _deviceId;
    body["token"]   = _authToken;
    body["cmd"]     = command;
    body["args"]    = args;
    String bodyStr;
    serializeJson(body, bodyStr);

    int code = http.POST(bodyStr);
    http.end();
    return code == 200;
}

// ---- Update UI ----
void EcovacsApp::_updateUI(const EcovacsState& state) {
    lv_label_set_text(_status_label, _statusStr(state.status));
    lv_bar_set_value(_battery_bar, state.battery, LV_ANIM_ON);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", state.battery);
    lv_label_set_text(_battery_label, buf);
    snprintf(buf, sizeof(buf), "Zone: %d m²", state.cleanedArea);
    lv_label_set_text(_area_label, buf);
    snprintf(buf, sizeof(buf), "Mode: %s", state.cleanMode.c_str());
    lv_label_set_text(_mode_label, buf);
}

const char* EcovacsApp::_statusStr(EcovacsStatus s) {
    switch(s) {
        case EcovacsStatus::IDLE:      return "En veille";
        case EcovacsStatus::CLEANING:  return LV_SYMBOL_REFRESH " Nettoyage...";
        case EcovacsStatus::RETURNING: return LV_SYMBOL_HOME " Retour base";
        case EcovacsStatus::CHARGING:  return LV_SYMBOL_CHARGE " Charge";
        case EcovacsStatus::ERROR:     return LV_SYMBOL_WARNING " Erreur";
        default:                       return "Inconnu";
    }
}

// ---- Callbacks ----
void EcovacsApp::_onClean(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_sendCleanCommand("auto");
    lv_label_set_text(self->_status_label, LV_SYMBOL_REFRESH " Nettoyage...");
}
void EcovacsApp::_onReturn(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_sendCleanCommand("charge");
    lv_label_set_text(self->_status_label, LV_SYMBOL_HOME " Retour base...");
}
void EcovacsApp::_onPause(lv_event_t* e) {
    EcovacsApp* self = (EcovacsApp*)lv_event_get_user_data(e);
    self->_sendCleanCommand("stop");
    lv_label_set_text(self->_status_label, LV_SYMBOL_PAUSE " Pause");
}
