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
#define APP_KEY           "1520391301804" // app key publique connue
#define APP_SECRET        "6c319b2ea5df3fed1f2cfd78825f6e4b" // secret app publique

// ─── État robot ───────────────────────────────────────────────────────────────
struct EcovacsState {
    int  battery;       // 0-100
    char status[24];    // "idle", "cleaning", "returning", "charging", "error"
    char mode[24];      // "auto", "spot", "edge"
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
    _state.authenticated = false;  // force re-auth
}

// ─── Auth Ecovacs ──────────────────────────────────────────────────────────────
static bool ecovacs_auth() {
    // MD5 du mot de passe
    char pass_md5[33];
    md5_hex(_eco_pass, pass_md5);

    // Payload auth
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
        "{\"todo\":\"GetDeviceList\",\"auth\":{\"uid\":\"%s\",\"accessToken\":\"%s\","
        "\"userid\":\"%s\",\"appid\":\"ecovacs\",\"appkey\":\"%s\"}}",
        _state.user_id, _state.access_token, _state.user_id, APP_KEY);

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
    if (!devices || devices.size() == 0) return false;

    // Premier robot trouvé
    JsonObject dev = devices[0];
    strlcpy(_state.robot_id, dev["did"] | "", sizeof(_state.robot_id));
    _state.online = dev["online"] | false;
    Serial.printf("[ECOVACS] Robot: %s online=%d\n", _state.robot_id, _state.online);
    return true;
}

// ─── Requête commande/état via API ────────────────────────────────────────────
static bool ecovacs_cmd(const char *todo, const char *cmd_json, char *out, size_t out_len) {
    char url[256];
    snprintf(url, sizeof(url), "%s/iot/devmanager.do", ECOVACS_API_URL);

    char body[512];
    if (cmd_json && cmd_json[0])
        snprintf(body, sizeof(body),
            "{\"todo\":\"%s\",\"did\":\"%s\",\"auth\":{\"uid\":\"%s\",\"accessToken\":\"%s\"},"
            "\"cmd\":%s}",
            todo, _state.robot_id, _state.user_id, _state.access_token, cmd_json);
    else
        snprintf(body, sizeof(body),
            "{\"todo\":\"%s\",\"did\":\"%s\",\"auth\":{\"uid\":\"%s\",\"accessToken\":\"%s\"}}",
            todo, _state.robot_id, _state.user_id, _state.access_token);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    int rc = http.POST(body);
    if (rc == 200 && out) {
        String s = http.getString();
        strncpy(out, s.c_str(), out_len - 1);
        out[out_len - 1] = '\0';
    }
    http.end();
    return rc == 200;
}

// ─── Parse état robot ─────────────────────────────────────────────────────────
static void ecovacs_parse_state(const char *json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    // Batterie
    int bat = doc["ret"]["battery"]["power"] | doc["battery"] | -1;
    if (bat >= 0) _state.battery = bat;
    // Statut
    const char *st = doc["ret"]["clean"]["status"] | doc["status"] | nullptr;
    if (st) strlcpy(_state.status, st, sizeof(_state.status));
    const char *charging = doc["ret"]["chargeState"]["status"] | nullptr;
    if (charging) strlcpy(_state.status, strcmp(charging, "idle") == 0 ? "charging" : charging, sizeof(_state.status));
}

// ─── Mise à jour UI ───────────────────────────────────────────────────────────
static void refresh_ui() {
    if (!_scr) return;
    if (_bar_battery) lv_bar_set_value(_bar_battery, _state.battery, LV_ANIM_ON);
    if (_lbl_battery) {
        char buf[16]; snprintf(buf, sizeof(buf), "%d%%", _state.battery);
        lv_label_set_text(_lbl_battery, buf);
        uint32_t col = _state.battery > 50 ? C_GREEN : (_state.battery > 20 ? C_ORANGE : C_ERR);
        lv_obj_set_style_text_color(_lbl_battery, lv_color_hex(col), 0);
    }
    if (_lbl_state) {
        const char *st_icons[] = {
            "idle",     LV_SYMBOL_PAUSE " En attente",
            "cleaning", LV_SYMBOL_REFRESH " Nettoyage",
            "returning",LV_SYMBOL_HOME " Retour base",
            "charging", LV_SYMBOL_CHARGE " En charge",
            "error",    LV_SYMBOL_WARNING " Erreur",
            nullptr, nullptr
        };
        const char *disp = _state.status;
        for (int i = 0; st_icons[i]; i += 2)
            if (strcmp(_state.status, st_icons[i]) == 0) { disp = st_icons[i+1]; break; }
        lv_label_set_text(_lbl_state, disp);
    }
    if (_lbl_mode && _state.mode[0]) lv_label_set_text(_lbl_mode, _state.mode);
}

// ─── FreeRTOS fetch ───────────────────────────────────────────────────────────
struct EcoFetch { bool ok; char err[48]; };
static EcoFetch _efres;

static void on_eco_done(void *) {
    _fetch_running = false;
    if (!_scr) return;
    if (!_efres.ok) {
        if (_lbl_status) lv_label_set_text(_lbl_status, _efres.err);
        return;
    }
    if (_lbl_status) lv_label_set_text(_lbl_status, _state.online ? "" : "Robot hors ligne");
    refresh_ui();
}

static void eco_fetch_task(void *) {
    _efres = {};
    if (!WiFi.isConnected()) {
        strlcpy(_efres.err, "WiFi non connecte", sizeof(_efres.err));
        lv_async_call(on_eco_done, nullptr); vTaskDelete(NULL); return;
    }
    if (!_creds_ready) {
        strlcpy(_efres.err, "Creds Ecovacs manquants (PWA)", sizeof(_efres.err));
        lv_async_call(on_eco_done, nullptr); vTaskDelete(NULL); return;
    }
    // Auth si nécessaire
    if (!_state.authenticated) {
        if (!ecovacs_auth()) {
            strlcpy(_efres.err, "Erreur auth Ecovacs", sizeof(_efres.err));
            lv_async_call(on_eco_done, nullptr); vTaskDelete(NULL); return;
        }
    }
    // Liste devices si robot_id vide
    if (_state.robot_id[0] == '\0') {
        if (!ecovacs_get_device_list()) {
            strlcpy(_efres.err, "Aucun robot trouve", sizeof(_efres.err));
            lv_async_call(on_eco_done, nullptr); vTaskDelete(NULL); return;
        }
    }
    // Batterie
    char resp[512];
    if (ecovacs_cmd("GetBatteryInfo", nullptr, resp, sizeof(resp))) {
        ecovacs_parse_state(resp);
    }
    // Statut nettoyage
    if (ecovacs_cmd("GetCleanState", nullptr, resp, sizeof(resp))) {
        ecovacs_parse_state(resp);
    }
    _efres.ok = true;
    lv_async_call(on_eco_done, nullptr);
    vTaskDelete(NULL);
}

static void start_fetch() {
    if (_fetch_running) return;
    _fetch_running = true;
    if (_lbl_status) lv_label_set_text(_lbl_status, "Chargement...");
    xTaskCreatePinnedToCore(eco_fetch_task, "eco_fetch", 8192, nullptr, 1, nullptr, 0);
}

// ─── Commandes boutons ────────────────────────────────────────────────────────
static void cmd_task(void *param) {
    const char *cmd = (const char *)param;
    if (strcmp(cmd, "clean") == 0)
        ecovacs_cmd("Clean", "{\"act\":\"start\",\"type\":\"auto\"}", nullptr, 0);
    else if (strcmp(cmd, "stop") == 0)
        ecovacs_cmd("Clean", "{\"act\":\"stop\"}", nullptr, 0);
    else if (strcmp(cmd, "charge") == 0)
        ecovacs_cmd("Charge", "{\"act\":\"go\"}", nullptr, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    // Refresh état après commande
    eco_fetch_task(nullptr);
    vTaskDelete(NULL);
}

static void btn_clean_cb(lv_event_t *)  { xTaskCreatePinnedToCore(cmd_task, "eco_cmd", 6144, (void*)"clean",  1, nullptr, 0); }
static void btn_stop_cb(lv_event_t *)   { xTaskCreatePinnedToCore(cmd_task, "eco_cmd", 6144, (void*)"stop",   1, nullptr, 0); }
static void btn_charge_cb(lv_event_t *) { xTaskCreatePinnedToCore(cmd_task, "eco_cmd", 6144, (void*)"charge", 1, nullptr, 0); }

// ─── Close ────────────────────────────────────────────────────────────────────
static void do_close() {
    if (!_app_active) return;
    _app_active    = false;
    _fetch_running = false;
    orchestrator_set_app(APP_LAUNCHER);

    lv_obj_t *scr_old = _scr;
    _scr           = nullptr;
    _lbl_status    = nullptr; _lbl_battery = nullptr; _lbl_mode = nullptr;
    _lbl_state     = nullptr; _bar_battery = nullptr;
    _btn_clean     = nullptr; _btn_stop = nullptr; _btn_charge = nullptr;

    // Lance l'animation de retour vers le launcher.
    // lv_scr_load_anim() prend ownership de scr_old : on le supprime après le
    // délai d'animation (300 ms) + 50 ms de marge, sans dépendre de callbacks
    // d'animation non disponibles dans toutes les versions LVGL 9.
    lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    if (scr_old) lv_obj_del_delayed(scr_old, 400);

    Serial.println("[APP/ECOVACS] Fermee");
}

static void back_cb(lv_event_t *) { do_close(); }

// ─── Start ────────────────────────────────────────────────────────────────────
void ecovacs_app_start() {
    load_creds();
    orchestrator_set_app(APP_ECOVACS);
    _app_active = true;
    _last_fetch = 0;

    _scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    // Bouton retour
    lv_obj_t *btn = lv_btn_create(_scr);
    lv_obj_set_size(btn, 52, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(C_TXT), 0);
    lv_obj_center(lbl_back);

    // Titre
    lv_obj_t *title = lv_label_create(_scr);
    lv_label_set_text(title, LV_SYMBOL_REFRESH " DEEBOT X8 Pro");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    // Status
    _lbl_status = lv_label_create(_scr);
    lv_label_set_text(_lbl_status, _creds_ready ? "" : "Configurer compte Ecovacs dans la PWA");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(_creds_ready ? C_MUTED : C_ERR), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 82);

    // Carte état robot
    lv_obj_t *card = lv_obj_create(_scr);
    lv_obj_set_size(card, 420, 140);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x30363d), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Icône robot
    lv_obj_t *icon = lv_label_create(card);
    lv_label_set_text(icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(C_ACCENT), 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);

    // État
    _lbl_state = lv_label_create(card);
    lv_label_set_text(_lbl_state, "---");
    lv_obj_set_style_text_font(_lbl_state, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_lbl_state, lv_color_hex(C_TXT), 0);
    lv_obj_align(_lbl_state, LV_ALIGN_TOP_MID, 20, 16);

    // Barre batterie
    _bar_battery = lv_bar_create(card);
    lv_obj_set_size(_bar_battery, 180, 12);
    lv_obj_align(_bar_battery, LV_ALIGN_BOTTOM_MID, 20, -24);
    lv_bar_set_range(_bar_battery, 0, 100);
    lv_bar_set_value(_bar_battery, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_bar_battery, lv_color_hex(0x30363d), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar_battery, lv_color_hex(C_GREEN), LV_PART_INDICATOR);

    _lbl_battery = lv_label_create(card);
    lv_label_set_text(_lbl_battery, "--%");
    lv_obj_set_style_text_font(_lbl_battery, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_lbl_battery, lv_color_hex(C_GREEN), 0);
    lv_obj_align(_lbl_battery, LV_ALIGN_BOTTOM_MID, 20, -6);

    // Mode
    _lbl_mode = lv_label_create(card);
    lv_label_set_text(_lbl_mode, "");
    lv_obj_set_style_text_font(_lbl_mode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_mode, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_mode, LV_ALIGN_TOP_MID, 20, 42);

    // Boutons de commande
    const struct { const char *lbl; uint32_t col; lv_event_cb_t cb; lv_obj_t **ref; } btns[] = {
        { LV_SYMBOL_REFRESH " Nettoyer", C_GREEN,  btn_clean_cb,  &_btn_clean  },
        { LV_SYMBOL_PAUSE   " Arreter",  C_ORANGE, btn_stop_cb,   &_btn_stop   },
        { LV_SYMBOL_CHARGE  " Base",     C_ACCENT, btn_charge_cb, &_btn_charge },
    };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(_scr);
        lv_obj_set_size(b, 136, 52);
        lv_obj_align(b, LV_ALIGN_BOTTOM_MID, (i - 1) * 148, -16);
        lv_obj_set_style_bg_color(b, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_color(b, lv_color_hex(btns[i].col), 0);
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_add_event_cb(b, btns[i].cb, LV_EVENT_CLICKED, NULL);
        *btns[i].ref = b;
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, btns[i].lbl);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(btns[i].col), 0);
        lv_obj_center(l);
        if (!_creds_ready) lv_obj_add_state(b, LV_STATE_DISABLED);
    }

    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    Serial.println("[APP/ECOVACS] Ouverte");
    if (_creds_ready) start_fetch();
}

// ─── Tick ─────────────────────────────────────────────────────────────────────
void ecovacs_app_tick() {
    if (!_app_active || !_scr) return;
    if (!_creds_ready) return;
    uint32_t now = millis();
    if (!_fetch_running && (_last_fetch == 0 || (now - _last_fetch) >= 30000UL)) {
        _last_fetch = now;
        start_fetch();
    }
}

void ecovacs_app_stop() { do_close(); }
