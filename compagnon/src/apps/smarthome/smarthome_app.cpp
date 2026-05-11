/**
 * smarthome_app.cpp — Domotique Tuya : lumières, prises, capteurs
 *
 * Architecture :
 *   - FreeRTOS task (core 0) pour les appels HTTP Tuya
 *   - lv_async_call() pour re-entrer dans le thread LVGL
 *   - Devices chargés au démarrage, rafraîchis toutes les 60 s
 *   - Max 12 devices affichés sur 2 colonnes scrollables
 */
#include "smarthome_app.h"
#include "../../config/nvs_config.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include "../../net/tuya_api.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <string.h>

// ─── Thème ────────────────────────────────────────────────────────────────────
#define C_BG      0x0d1117
#define C_CARD    0x1c2128
#define C_CARD_ON 0x1a3a2a
#define C_TXT     0xe6edf3
#define C_MUTED   0x8b949e
#define C_ACCENT  0x3fb950
#define C_ERR     0xf85149
#define C_SENSOR  0x388bfd

// ─── Modèle device ────────────────────────────────────────────────────────────
#define MAX_DEVICES 12
struct TuyaDevice {
    char id[32];
    char name[32];
    char category[8];   // "kg" prise, "dj" lumière, "wsdcg" capteur
    bool online;
    bool power;         // switch_1 / switch_led
    int  brightness;    // 0-1000 (lumières)
    float temp;         // capteurs
    float humidity;     // capteurs
    bool is_sensor;     // true si catégorie capteur
};

static TuyaDevice _devices[MAX_DEVICES];
static int        _dev_count = 0;

// ─── État UI ──────────────────────────────────────────────────────────────────
static lv_obj_t *_scr         = nullptr;
static lv_obj_t *_lbl_status  = nullptr;
static lv_obj_t *_list_cont   = nullptr;
static lv_obj_t *_dev_cards[MAX_DEVICES] = {};
static lv_obj_t *_lbl_power[MAX_DEVICES] = {};
static lv_obj_t *_lbl_sensor[MAX_DEVICES] = {};

static bool           _app_active    = false;
static volatile bool  _fetch_running = false;
static uint32_t       _last_fetch    = 0;
static lv_obj_t      *_scr_to_delete = nullptr;

static char _tuya_id[48]  = {};
static char _tuya_sec[48] = {};
static bool _tuya_ready   = false;

// ─── Credentials ─────────────────────────────────────────────────────────────
static void load_credentials() {
    nvs_get_api_key(NVS_KEY_TUYA_ID,  _tuya_id,  sizeof(_tuya_id));
    nvs_get_api_key(NVS_KEY_TUYA_SEC, _tuya_sec, sizeof(_tuya_sec));
    _tuya_ready = (_tuya_id[0] != '\0' && _tuya_sec[0] != '\0');
}

void smarthome_set_tuya_id(const char *id)       { strlcpy(_tuya_id,  id,  sizeof(_tuya_id));  nvs_set_api_key(NVS_KEY_TUYA_ID,  id);  }
void smarthome_set_tuya_secret(const char *sec)  { strlcpy(_tuya_sec, sec, sizeof(_tuya_sec)); nvs_set_api_key(NVS_KEY_TUYA_SEC, sec); }

// ─── Parse devices ────────────────────────────────────────────────────────────
static void parse_devices(const char *json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    JsonArray arr = doc["result"]["list"].as<JsonArray>();
    if (!arr) arr = doc["result"].as<JsonArray>();
    _dev_count = 0;
    for (JsonObject d : arr) {
        if (_dev_count >= MAX_DEVICES) break;
        TuyaDevice &dev = _devices[_dev_count];
        strlcpy(dev.id,       d["id"]       | "", sizeof(dev.id));
        strlcpy(dev.name,     d["name"]     | "Device", sizeof(dev.name));
        strlcpy(dev.category, d["category"] | "", sizeof(dev.category));
        dev.online   = d["online"]     | false;
        dev.power    = false;
        dev.brightness = 0;
        dev.temp     = 0;
        dev.humidity = 0;
        dev.is_sensor = (strncmp(dev.category, "wsdcg", 5) == 0 ||
                         strncmp(dev.category, "zndb",  4) == 0 ||
                         strncmp(dev.category, "sensor",6) == 0);
        _dev_count++;
    }
    Serial.printf("[SMARTHOME] %d devices charges\n", _dev_count);
}

// ─── Parse status d'un device ─────────────────────────────────────────────────
static void parse_status(int idx, const char *json) {
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;
    JsonArray arr = doc["result"].as<JsonArray>();
    for (JsonObject s : arr) {
        const char *code = s["code"] | "";
        if (strcmp(code, "switch_1") == 0 || strcmp(code, "switch_led") == 0 ||
            strcmp(code, "switch") == 0)
            _devices[idx].power = s["value"] | false;
        else if (strcmp(code, "bright_value") == 0 || strcmp(code, "bright_value_v2") == 0)
            _devices[idx].brightness = s["value"] | 0;
        else if (strcmp(code, "va_temperature") == 0 || strcmp(code, "temp_current") == 0)
            _devices[idx].temp = (float)(s["value"] | 0) / 10.0f;
        else if (strcmp(code, "va_humidity") == 0 || strcmp(code, "humidity_value") == 0)
            _devices[idx].humidity = (float)(s["value"] | 0) / 10.0f;
    }
}

// ─── Mise à jour cartes UI (thread LVGL) ─────────────────────────────────────
static void refresh_cards() {
    for (int i = 0; i < _dev_count && i < MAX_DEVICES; i++) {
        if (!_dev_cards[i]) continue;
        TuyaDevice &d = _devices[i];
        bool on = d.power && d.online;
        lv_obj_set_style_bg_color(_dev_cards[i], lv_color_hex(on ? C_CARD_ON : C_CARD), 0);
        if (_lbl_power[i]) {
            if (d.is_sensor) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.1f C  %.0f%%", d.temp, d.humidity);
                lv_label_set_text(_lbl_power[i], buf);
                lv_obj_set_style_text_color(_lbl_power[i], lv_color_hex(C_SENSOR), 0);
            } else {
                lv_label_set_text(_lbl_power[i], on ? LV_SYMBOL_OK " ON" : LV_SYMBOL_CLOSE " OFF");
                lv_obj_set_style_text_color(_lbl_power[i], lv_color_hex(on ? C_ACCENT : C_MUTED), 0);
            }
        }
    }
}

// ─── Résultat fetch ───────────────────────────────────────────────────────────
struct FetchResult { bool ok; char err[48]; };
static FetchResult _fres;

static void on_fetch_done(void *) {
    _fetch_running = false;
    if (!_scr) return;
    if (!_fres.ok) {
        if (_lbl_status) lv_label_set_text(_lbl_status, _fres.err);
        return;
    }
    if (_lbl_status) lv_label_set_text(_lbl_status, "");
    // Reconstruire les cartes si nécessaire
    if (_list_cont && lv_obj_get_child_count(_list_cont) == 0) {
        // Recréer cartes (premier fetch)
        for (int i = 0; i < _dev_count && i < MAX_DEVICES; i++) {
            lv_obj_t *card = lv_obj_create(_list_cont);
            lv_obj_set_size(card, 210, 72);
            lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(card, 12, 0);
            lv_obj_set_style_border_width(card, 1, 0);
            lv_obj_set_style_border_color(card, lv_color_hex(0x30363d), 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
            _dev_cards[i] = card;

            // Nom
            lv_obj_t *lbl_name = lv_label_create(card);
            lv_label_set_text(lbl_name, _devices[i].name);
            lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_name, lv_color_hex(C_TXT), 0);
            lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(lbl_name, 180);
            lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 10, 8);

            // État ON/OFF ou capteur
            lv_obj_t *lbl_pw = lv_label_create(card);
            lv_label_set_text(lbl_pw, "...");
            lv_obj_set_style_text_font(lbl_pw, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(lbl_pw, lv_color_hex(C_MUTED), 0);
            lv_obj_align(lbl_pw, LV_ALIGN_BOTTOM_LEFT, 10, -8);
            _lbl_power[i] = lbl_pw;

            // Hors ligne
            if (!_devices[i].online) {
                lv_obj_t *lbl_off = lv_label_create(card);
                lv_label_set_text(lbl_off, "offline");
                lv_obj_set_style_text_font(lbl_off, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(lbl_off, lv_color_hex(C_ERR), 0);
                lv_obj_align(lbl_off, LV_ALIGN_TOP_RIGHT, -8, 8);
            }

            // Tap → toggle (pas pour capteurs)
            if (!_devices[i].is_sensor && _devices[i].online) {
                lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(card, [](lv_event_t *e) {
                    int idx = (int)(intptr_t)lv_event_get_user_data(e);
                    if (idx < 0 || idx >= _dev_count) return;
                    bool new_val = !_devices[idx].power;
                    const char *code = (strncmp(_devices[idx].category, "dj", 2) == 0) ? "switch_led" : "switch_1";
                    // Appel async (FreeRTOS)
                    struct CmdArgs { char id[32]; char code[16]; bool val; };
                    static CmdArgs args;
                    strlcpy(args.id, _devices[idx].id, sizeof(args.id));
                    strlcpy(args.code, code, sizeof(args.code));
                    args.val = new_val;
                    xTaskCreatePinnedToCore([](void *p) {
                        CmdArgs *a = (CmdArgs *)p;
                        tuya_api_send_command_bool(a->id, a->code, a->val);
                        vTaskDelete(NULL);
                    }, "tuya_cmd", 4096, &args, 1, nullptr, 0);
                    _devices[idx].power = new_val;
                    refresh_cards();
                }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            }
        }
    }
    refresh_cards();
}

// ─── Tâche FreeRTOS — fetch HTTP ──────────────────────────────────────────────
static void fetch_task(void *) {
    _fres = {};
    if (!WiFi.isConnected()) {
        strlcpy(_fres.err, "WiFi non connecte", sizeof(_fres.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }
    if (!_tuya_ready) {
        strlcpy(_fres.err, "Cles Tuya manquantes (PWA)", sizeof(_fres.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }

    // Auth Tuya
    if (!tuya_api_auth(_tuya_id, _tuya_sec)) {
        strlcpy(_fres.err, "Auth Tuya echouee", sizeof(_fres.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }

    // Devices list (premier fetch uniquement)
    if (_dev_count == 0) {
        char *buf = (char *)malloc(8192);
        if (buf && tuya_api_get_devices(buf, 8192)) {
            parse_devices(buf);
        }
        free(buf);
    }

    // Status de chaque device
    for (int i = 0; i < _dev_count && _app_active; i++) {
        char *buf = (char *)malloc(2048);
        if (buf && tuya_api_get_status(_devices[i].id, buf, 2048)) {
            parse_status(i, buf);
        }
        free(buf);
        vTaskDelay(pdMS_TO_TICKS(100)); // Throttle
    }

    _fres.ok = true;
    lv_async_call(on_fetch_done, nullptr);
    vTaskDelete(NULL);
}

static void start_fetch() {
    if (_fetch_running) return;
    _fetch_running = true;
    xTaskCreatePinnedToCore(fetch_task, "smarthome_fetch", 8192, nullptr, 1, nullptr, 0);
}

// ─── Anim ready (suppression écran) ──────────────────────────────────────────
static void anim_ready_cb(lv_anim_t *a) {
    LV_UNUSED(a);
    if (_scr_to_delete) { lv_obj_delete(_scr_to_delete); _scr_to_delete = nullptr; }
}

static void do_close() {
    if (!_app_active) return;
    _app_active    = false;
    _fetch_running = false;
    orchestrator_set_app(APP_LAUNCHER);
    _scr_to_delete = _scr;
    _scr           = nullptr;
    _lbl_status    = nullptr;
    _list_cont     = nullptr;
    for (int i = 0; i < MAX_DEVICES; i++) { _dev_cards[i] = nullptr; _lbl_power[i] = nullptr; _lbl_sensor[i] = nullptr; }
    ui_launcher_return();             // retour au launcher avec animation
    lv_obj_delete_delayed(_scr_to_delete, 350); // supprime l'écran après l'anim
    _scr_to_delete = nullptr;
    Serial.println("[APP/SMARTHOME] Fermee");
}

static void back_cb(lv_event_t *) { do_close(); }

// ─── Start ─────────────────────────────────────────────────────────────────────
void smarthome_app_start() {
    load_credentials();
    orchestrator_set_app(APP_SMARTHOME);
    _app_active    = true;
    _fetch_running = false;
    _dev_count     = 0;
    _scr_to_delete = nullptr;
    memset(_dev_cards, 0, sizeof(_dev_cards));
    memset(_lbl_power, 0, sizeof(_lbl_power));
    memset(_lbl_sensor, 0, sizeof(_lbl_sensor));

    // ─── Écran ────────────────────────────────────────────────────────────────
    _scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t *hdr = lv_obj_create(_scr);
    lv_obj_set_size(hdr, LV_HOR_RES, 48);
    lv_obj_align(hdr, LV_ALIGN_TOP_MID, 0, 0);
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
    lv_label_set_text(lbl_title, LV_SYMBOL_HOME " Maison");
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

    // Status label
    _lbl_status = lv_label_create(_scr);
    lv_label_set_text(_lbl_status, "Chargement...");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 56);

    // ─── Liste scrollable des devices (2 colonnes) ────────────────────────────
    _list_cont = lv_obj_create(_scr);
    lv_obj_set_size(_list_cont, LV_HOR_RES, LV_VER_RES - 56);
    lv_obj_align(_list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(_list_cont, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_list_cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_list_cont, 0, 0);
    lv_obj_set_style_pad_all(_list_cont, 8, 0);
    lv_obj_set_layout(_list_cont, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(_list_cont, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(_list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(_list_cont, 8, 0);
    lv_obj_set_style_pad_column(_list_cont, 8, 0);

    // Chargement écran
    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    start_fetch();
}

void smarthome_app_tick() {
    if (!_app_active) return;
    uint32_t now = millis();
    if (now - _last_fetch > 60000) {
        _last_fetch = now;
        start_fetch();
    }
}

void smarthome_app_stop() { do_close(); }
