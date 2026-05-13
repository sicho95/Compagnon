/**
 * ecovacs_app.cpp — Ecovacs DEEBOT X8 Pro Omni via API cloud
 *
 * API REST Ecovacs (non-officielle, bien documentée) :
 *   Auth  : POST https://gl-eu-openapi.ecovacs.com/v1/private/eu/user/login
 *   XMPP  : commandes via API REST JSON
 *
 * Référence : https://github.com/DeebotUniverse/Deebot-4-Home-Assistant
 */
#include "ecovacs_app.h"
#include "../../config/nvs_config.h"
#include "../../config/ui_config.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <lvgl.h>
#include <string.h>

// ─── Thème ────────────────────────────────────────────────────────────────────
#define C_BG       0x0d1117
#define C_CARD     0x1c2128
#define C_TXT      0xe6edf3
#define C_MUTED    0x8b949e
#define C_ACCENT   0x388bfd
#define C_ERR      0xf85149
#define C_GREEN    0x3fb950
#define C_ORANGE   0xd29922

// ─── Ecovacs API constants ────────────────────────────────────────────────────
#define ECOVACS_AUTH_URL  "https://gl-eu-openapi.ecovacs.com/v1/private/eu/user/login"
#define ECOVACS_API_URL   "https://portal-eu.ecouser.net/api"
#define APP_KEY           "1520391301804"
#define APP_SECRET        "6c319b2ea5df3fed1f2cfd78825f6e4b"

// ─── État robot ───────────────────────────────────────────────────────────────
struct EcovacsState {
    int  battery;
    char status[24];
    char mode[24];
    bool online;
    char robot_id[32];
    char uid[64];
    char access_token[128];
    char user_id[64];
    bool authenticated;
};
static EcovacsState _state = {};

// ─── État UI ──────────────────────────────────────────────────────────────────
static lv_obj_t *_scr          = nullptr;
static lv_obj_t *_lbl_status   = nullptr;
static lv_obj_t *_lbl_battery  = nullptr;
static lv_obj_t *_lbl_mode     = nullptr;
static lv_obj_t *_lbl_state    = nullptr;
static lv_obj_t *_bar_battery  = nullptr;
static lv_obj_t *_btn_clean    = nullptr;
static lv_obj_t *_btn_stop     = nullptr;
static lv_obj_t *_btn_charge   = nullptr;

static bool           _app_active    = false;
static volatile bool  _fetch_running = false;
static uint32_t       _last_fetch    = 0;

static char _eco_user[64] = {};
static char _eco_pass[64] = {};
static bool _creds_ready  = false;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static void md5_hex(const char *str, char *out32) {
    uint8_t hash[16];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const uint8_t *)str, strlen(str));
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    for (int i = 0; i < 16; i++) sprintf(out32 + 2*i, "%02x", hash[i]);
    out32[32] = '\0';
}

static void load_creds() {
    nvs_get_api_key(NVS_KEY_ECOVACS_U, _eco_user, sizeof(_eco_user));
    nvs_get_api_key(NVS_KEY_ECOVACS_P, _eco_pass, sizeof(_eco_pass));
    _creds_ready = (_eco_user[0] != '\0' && _eco_pass[0] != '\0');
}

void ecovacs_set_credentials(const char *user, const char *pass) {
    strlcpy(_eco_user, user, sizeof(_eco_user));
    strlcpy(_eco_pass, pass, sizeof(_eco_pass));
    nvs_set_api_key(NVS_KEY_ECOVACS_U, user);
    nvs_set_api_key(NVS_KEY_ECOVACS_P, pass);
    _creds_ready = true;
    _state.authenticated = false;
}

// ─── Auth Ecovacs ──────────────────────────────────────────────────────────────
static bool ecovacs_auth() {
    char pass_md5[33];
    md5_hex(_eco_pass, pass_md5);

    char body[512];
    snprintf(body, sizeof(body),
        "{\"account\":\"%s\",\"password\":\"%s\","
        "\"requestId\":\"12345678901234567890123456789012\","
        "\"authTimespan\":1000000000000,"
        "\"authTimeZone\":\"GMT+1\"}",
        _eco_user, pass_md5);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(ECOVACS_AUTH_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent",   "Dalvik/2.1.0");
    int rc = http.POST(body);
    if (rc != 200) { Serial.printf("[ECOVACS] Auth HTTP %d\n", rc); http.end(); return false; }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    if (doc["code"] | -1) {
        Serial.printf("[ECOVACS] Auth error code: %d\n", doc["code"] | -1);
        return false;
    }
    strlcpy(_state.access_token, doc["data"]["accessToken"] | "", sizeof(_state.access_token));
    strlcpy(_state.user_id,      doc["data"]["uid"]         | "", sizeof(_state.user_id));
    _state.authenticated = (_state.access_token[0] != '\0');
    Serial.printf("[ECOVACS] Auth OK uid=%s\n", _state.user_id);
    return _state.authenticated;
}

// ─── Récupère la liste des bots et l'état ─────────────────────────────────────
static bool ecovacs_get_device_list() {
    char url[256];
    snprintf(url, sizeof(url), "%s/users/user.do", ECOVACS_API_URL);
    char body[256];
    snprintf(body, sizeof(body),
        "{\"todo\":\"GetDeviceList\",\"auth\":{\"uid\":\"%s\",\"accessToken\":\"%s\"}}",
        _state.user_id, _state.access_token);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(body);
    if (rc != 200) { http.end(); return false; }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

    JsonArray devices = doc["devices"].as<JsonArray>();
    if (!devices) return false;
    for (JsonObject d : devices) {
        strlcpy(_state.robot_id, d["did"] | "", sizeof(_state.robot_id));
        _state.online = d["online"] | false;
        break;  // premier robot seulement
    }
    return (_state.robot_id[0] != '\0');
}

static bool ecovacs_get_clean_info() {
    char url[256];
    snprintf(url, sizeof(url), "%s/iot/devmanager.do", ECOVACS_API_URL);
    char body[512];
    snprintf(body, sizeof(body),
        "{\"did\":\"%s\",\"td\":\"q\",\"auth\":{\"uid\":\"%s\",\"accessToken\":\"%s\"},"
        "\"cmdName\":\"getCleanInfo\"}",
        _state.robot_id, _state.user_id, _state.access_token);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(body);
    if (rc != 200) { http.end(); return false; }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;

    const char *st = doc["resp"]["body"]["data"]["state"] | "idle";
    strlcpy(_state.status, st, sizeof(_state.status));
    strlcpy(_state.mode,   doc["resp"]["body"]["data"]["cleanState"]["motionState"] | "-", sizeof(_state.mode));
    return true;
}

static bool ecovacs_get_battery() {
    char url[256];
    snprintf(url, sizeof(url), "%s/iot/devmanager.do", ECOVACS_API_URL);
    char body[512];
    snprintf(body, sizeof(body),
        "{\"did\":\"%s\",\"td\":\"q\",\"auth\":{\"uid\":\"%s\",\"accessToken\":\"%s\"},"
        "\"cmdName\":\"getBattery\"}",
        _state.robot_id, _state.user_id, _state.access_token);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(body);
    if (rc != 200) { http.end(); return false; }

    String resp = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    _state.battery = doc["resp"]["body"]["data"]["value"] | 0;
    return true;
}

// ─── Envoie une commande ──────────────────────────────────────────────────────
static bool ecovacs_send_cmd(const char *cmdName) {
    char url[256];
    snprintf(url, sizeof(url), "%s/iot/devmanager.do", ECOVACS_API_URL);
    char body[512];
    snprintf(body, sizeof(body),
        "{\"did\":\"%s\",\"td\":\"q\",\"auth\":{\"uid\":\"%s\",\"accessToken\":\"%s\"},"
        "\"cmdName\":\"%s\"}",
        _state.robot_id, _state.user_id, _state.access_token, cmdName);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(body);
    http.end();
    return (rc == 200);
}

// ─── Mise à jour UI (thread LVGL) ────────────────────────────────────────────
static void update_ui() {
    if (!_scr) return;

    if (_lbl_battery) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", _state.battery);
        lv_label_set_text(_lbl_battery, buf);
    }
    if (_bar_battery) {
        lv_bar_set_value(_bar_battery, _state.battery, LV_ANIM_ON);
        lv_color_t col = (_state.battery > 50) ? lv_color_hex(C_GREEN)
                       : (_state.battery > 20) ? lv_color_hex(C_ORANGE)
                       : lv_color_hex(C_ERR);
        lv_obj_set_style_bg_color(_bar_battery, col, LV_PART_INDICATOR);
    }
    if (_lbl_state) lv_label_set_text(_lbl_state, _state.status);
    if (_lbl_mode)  lv_label_set_text(_lbl_mode,  _state.mode);
    if (_lbl_status) {
        lv_label_set_text(_lbl_status, _state.online ? "" : "Hors ligne");
        lv_obj_set_style_text_color(_lbl_status,
            lv_color_hex(_state.online ? C_GREEN : C_ERR), 0);
    }
}

// ─── Résultat fetch ───────────────────────────────────────────────────────────
struct EcoFetchResult { bool ok; char err[48]; };
static EcoFetchResult _fres;

static void on_fetch_done(void *) {
    _fetch_running = false;
    if (!_scr) return;
    if (!_fres.ok) {
        if (_lbl_status) {
            lv_label_set_text(_lbl_status, _fres.err);
            lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_ERR), 0);
        }
        return;
    }
    update_ui();
}

static void fetch_task(void *) {
    _fres = {};
    if (!WiFi.isConnected()) {
        strlcpy(_fres.err, "WiFi non connecte", sizeof(_fres.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }
    if (!_creds_ready) {
        strlcpy(_fres.err, "Identifiants manquants", sizeof(_fres.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }
    if (!_state.authenticated && !ecovacs_auth()) {
        strlcpy(_fres.err, "Auth Ecovacs echouee", sizeof(_fres.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }
    if (_state.robot_id[0] == '\0' && !ecovacs_get_device_list()) {
        strlcpy(_fres.err, "Robot introuvable", sizeof(_fres.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }
    ecovacs_get_battery();
    ecovacs_get_clean_info();
    _fres.ok = true;
    lv_async_call(on_fetch_done, nullptr);
    vTaskDelete(NULL);
}

static void start_fetch() {
    if (_fetch_running) return;
    _fetch_running = true;
    xTaskCreatePinnedToCore(fetch_task, "eco_fetch", 8192, nullptr, 1, nullptr, 0);
}

// ─── Callbacks boutons commande ───────────────────────────────────────────────
static void cmd_task(void *arg) {
    const char *cmd = (const char *)arg;
    ecovacs_send_cmd(cmd);
    vTaskDelay(pdMS_TO_TICKS(1500));
    start_fetch();
    vTaskDelete(NULL);
}

static void send_cmd(const char *cmd) {
    xTaskCreatePinnedToCore(cmd_task, "eco_cmd", 4096, (void *)cmd, 1, nullptr, 0);
}

static void on_clean (lv_event_t *) { send_cmd("clean");  }
static void on_stop  (lv_event_t *) { send_cmd("stop");   }
static void on_charge(lv_event_t *) { send_cmd("charge"); }

// ─── do_close ─────────────────────────────────────────────────────────────────
static void do_close() {
    if (!_app_active) return;
    _app_active    = false;
    _fetch_running = false;
    if (_scr) { lv_obj_delete_delayed(_scr, 350); _scr = nullptr; }
    _lbl_status = _lbl_battery = _lbl_mode = _lbl_state = nullptr;
    _bar_battery = _btn_clean = _btn_stop = _btn_charge = nullptr;
    orchestrator_set_app(APP_LAUNCHER);
    ui_launcher_return();
    Serial.println("[APP/ECOVACS] Fermee");
}

static void back_cb(lv_event_t *) { do_close(); }

// ─── Build UI ─────────────────────────────────────────────────────────────────
static void build_ui() {
    _scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    // ── Header (safe area : UI_X / APP_Y / UI_W) ──────────────────────────────
    lv_obj_t *hdr = lv_obj_create(_scr);
    lv_obj_set_size(hdr, UI_W, 48);
    lv_obj_set_pos(hdr, UI_X, APP_Y);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // Bouton retour
    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 44, 44);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_radius(btn_back, 22, 0);
    lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    // Titre
    lv_obj_t *lbl_title = lv_label_create(hdr);
    lv_label_set_text(lbl_title, LV_SYMBOL_REFRESH " Ecovacs");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(C_TXT), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    // Bouton refresh
    lv_obj_t *btn_ref = lv_btn_create(hdr);
    lv_obj_set_size(btn_ref, 44, 44);
    lv_obj_align(btn_ref, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(btn_ref, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_radius(btn_ref, 22, 0);
    lv_obj_add_event_cb(btn_ref, [](lv_event_t *) { start_fetch(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl_ref = lv_label_create(btn_ref);
    lv_label_set_text(lbl_ref, LV_SYMBOL_REFRESH);
    lv_obj_center(lbl_ref);

    // ── Carte état robot ──────────────────────────────────────────────────────
    // Centrée horizontalement dans la safe area
    int card_w = UI_W - 32;  // marge interne 16px de chaque côté
    lv_obj_t *card = lv_obj_create(_scr);
    lv_obj_set_size(card, card_w, 160);
    lv_obj_set_pos(card, UI_X + 16, APP_Y + 56);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x30363d), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Batterie : barre + label
    _bar_battery = lv_bar_create(card);
    lv_obj_set_size(_bar_battery, card_w - 80, 12);
    lv_obj_align(_bar_battery, LV_ALIGN_TOP_LEFT, 8, 16);
    lv_bar_set_range(_bar_battery, 0, 100);
    lv_bar_set_value(_bar_battery, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_bar_battery, lv_color_hex(0x30363d), 0);
    lv_obj_set_style_bg_color(_bar_battery, lv_color_hex(C_GREEN), LV_PART_INDICATOR);

    _lbl_battery = lv_label_create(card);
    lv_label_set_text(_lbl_battery, "--%%");
    lv_obj_set_style_text_font(_lbl_battery, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_lbl_battery, lv_color_hex(C_TXT), 0);
    lv_obj_align(_lbl_battery, LV_ALIGN_TOP_RIGHT, -8, 8);

    // État
    _lbl_state = lv_label_create(card);
    lv_label_set_text(_lbl_state, "--");
    lv_obj_set_style_text_font(_lbl_state, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(_lbl_state, lv_color_hex(C_ACCENT), 0);
    lv_obj_align(_lbl_state, LV_ALIGN_CENTER, 0, -10);

    // Mode
    _lbl_mode = lv_label_create(card);
    lv_label_set_text(_lbl_mode, "--");
    lv_obj_set_style_text_font(_lbl_mode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_mode, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_mode, LV_ALIGN_CENTER, 0, 18);

    // Status (online/offline)
    _lbl_status = lv_label_create(card);
    lv_label_set_text(_lbl_status, "Chargement...");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_BOTTOM_MID, 0, -8);

    // ── Boutons de commande ───────────────────────────────────────────────────
    // Répartis horizontalement dans la safe area
    int btn_y   = APP_Y + 56 + 168 + 12;  // sous la carte état
    int btn_w   = (UI_W - 32 - 16) / 3;   // 3 boutons, gap=8 entre eux
    int btn_x0  = UI_X + 16;

    // Nettoyer
    _btn_clean = lv_btn_create(_scr);
    lv_obj_set_size(_btn_clean, btn_w, 48);
    lv_obj_set_pos(_btn_clean, btn_x0, btn_y);
    lv_obj_set_style_bg_color(_btn_clean, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_radius(_btn_clean, 10, 0);
    lv_obj_add_event_cb(_btn_clean, on_clean, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lc = lv_label_create(_btn_clean);
    lv_label_set_text(lc, LV_SYMBOL_PLAY " Nettoyer");
    lv_obj_set_style_text_font(lc, &lv_font_montserrat_12, 0);
    lv_obj_center(lc);

    // Stop
    _btn_stop = lv_btn_create(_scr);
    lv_obj_set_size(_btn_stop, btn_w, 48);
    lv_obj_set_pos(_btn_stop, btn_x0 + btn_w + 8, btn_y);
    lv_obj_set_style_bg_color(_btn_stop, lv_color_hex(C_ERR), 0);
    lv_obj_set_style_radius(_btn_stop, 10, 0);
    lv_obj_add_event_cb(_btn_stop, on_stop, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *ls = lv_label_create(_btn_stop);
    lv_label_set_text(ls, LV_SYMBOL_STOP " Stop");
    lv_obj_set_style_text_font(ls, &lv_font_montserrat_12, 0);
    lv_obj_center(ls);

    // Charge
    _btn_charge = lv_btn_create(_scr);
    lv_obj_set_size(_btn_charge, btn_w, 48);
    lv_obj_set_pos(_btn_charge, btn_x0 + (btn_w + 8) * 2, btn_y);
    lv_obj_set_style_bg_color(_btn_charge, lv_color_hex(C_ORANGE), 0);
    lv_obj_set_style_radius(_btn_charge, 10, 0);
    lv_obj_add_event_cb(_btn_charge, on_charge, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lb = lv_label_create(_btn_charge);
    lv_label_set_text(lb, LV_SYMBOL_CHARGE " Base");
    lv_obj_set_style_text_font(lb, &lv_font_montserrat_12, 0);
    lv_obj_center(lb);
}

// ─── API publique ─────────────────────────────────────────────────────────────
void ecovacs_app_start() {
    if (_app_active) return;
    load_creds();
    orchestrator_set_app(APP_ECOVACS);
    _app_active    = true;
    _fetch_running = false;

    build_ui();
    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    start_fetch();
    Serial.println("[APP/ECOVACS] Ouverte");
}

void ecovacs_app_tick() {
    if (!_app_active) return;
    uint32_t now = millis();
    if (now - _last_fetch > 30000) {
        _last_fetch = now;
        start_fetch();
    }
}

void ecovacs_app_stop() { do_close(); }
