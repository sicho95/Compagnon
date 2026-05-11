/**
 * Afficher prévisions météo J+3 via api.meteo-concept.com.
 * Position GPS : BLE téléphone → dernière position NVS → Paris (fallback final).
 * Fetch async via FreeRTOS pour ne pas bloquer le thread LVGL.
 *
 * Clé API Météo Concept : stockée en NVS via nvs_config (namespace "compagnon",
 *   clé NVS_KEY_METEO = "METEO_CONCEPT_API_KEY").
 *   Écriture depuis la PWA via BLE → nvs_set_api_key(NVS_KEY_METEO, val).
 */
#include "meteo_app.h"
#include "../../config/nvs_config.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include "../../net/ble_mgr.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <Preferences.h>
#include <time.h>

// ─── Clé API Météo Concept (chargée depuis NVS via nvs_config) ───────────────
char g_meteo_api_key[128] = {};

static void load_meteo_key() {
    nvs_get_api_key(NVS_KEY_METEO, g_meteo_api_key, sizeof(g_meteo_api_key));
}

void meteo_set_api_key(const char *key) {
    strlcpy(g_meteo_api_key, key, sizeof(g_meteo_api_key));
    nvs_set_api_key(NVS_KEY_METEO, key);
    Serial.printf("[METEO] Clé API mise à jour (%d chars)\n", (int)strlen(key));
}

// ─── Couleurs thème Météo ──────────────────────────────────────────────────────
#define C_BG   0x000000
#define C_TXT  0xFFCC44
#define C_MUTED 0xAA8822
#define C_CARD  0x1b5e20
#define C_ERR   0xFF4444

// ─── NVS pour la dernière position GPS connue ─────────────────────────────────
static Preferences _prefs;

static void nvs_save_pos(double lat, double lon) {
    _prefs.begin("meteo", false);
    _prefs.putDouble("lat", lat);
    _prefs.putDouble("lon", lon);
    _prefs.end();
}

static bool nvs_load_pos(double &lat, double &lon) {
    _prefs.begin("meteo", true);
    bool ok = _prefs.isKey("lat");
    if (ok) { lat = _prefs.getDouble("lat", 48.8566); lon = _prefs.getDouble("lon", 2.3522); }
    _prefs.end();
    return ok;
}

// ─── Prévision J+0..J+2 ───────────────────────────────────────────────────────
struct DayForecast { char date[12]; int tmin, tmax, code, rain10; };

// ─── Résultat fetch (partagé tâche FreeRTOS ↔ callback LVGL) ──────────────────
struct MeteoResult {
    bool ok;
    char city[32];
    DayForecast days[3];
    int nb_days;
    char err[48];
};
static MeteoResult _result;

// ─── État UI ──────────────────────────────────────────────────────────────────
static lv_obj_t *_scr        = nullptr;
static lv_obj_t *_lbl_gps    = nullptr;
static lv_obj_t *_lbl_status = nullptr;
static lv_obj_t *_cards[3]   = {};

static bool     _app_active     = false;
static uint32_t _last_fetch     = 0;
static double   _lat = 48.8566, _lon = 2.3522;
static bool     _gps_ok         = false;
static volatile bool _fetch_running = false;

// ─── Suppression différée (après fin d'animation de retour) ──────────────────
static lv_obj_t *_scr_to_delete = nullptr;

static void anim_ready_cb(lv_anim_t *a) {
    LV_UNUSED(a);
    if (_scr_to_delete) {
        lv_obj_del(_scr_to_delete);
        _scr_to_delete = nullptr;
    }
}

// ─── Noms jours / mois ────────────────────────────────────────────────────────
static const char *JOURS[] = {"dim","lun","mar","mer","jeu","ven","sam"};
static const char *MOIS[]  = {"","jan","fev","mar","avr","mai","juin",
                               "juil","aou","sep","oct","nov","dec"};

static const char *meteo_icon(int code) {
    if (code >= 1   && code <= 4)  return LV_SYMBOL_WIFI;
    if (code >= 10  && code <= 16) return LV_SYMBOL_WARNING;
    if (code >= 20  && code <= 48) return LV_SYMBOL_AUDIO;
    if (code >= 60  && code <= 78) return LV_SYMBOL_DOWN;
    if (code >= 100)               return LV_SYMBOL_UP;
    return LV_SYMBOL_WARNING;
}

static const char *meteo_label(int code) {
    if (code >= 1   && code <= 4)  return "Ensoleille";
    if (code >= 10  && code <= 16) return "Nuageux";
    if (code >= 20  && code <= 26) return "Pluie";
    if (code >= 40  && code <= 48) return "Forte pluie";
    if (code >= 60  && code <= 78) return "Neige";
    if (code >= 100)               return "Orage";
    return "Variable";
}

// ─── Mise à jour cartes (thread LVGL uniquement) ──────────────────────────────
static void refresh_cards() {
    for (int i = 0; i < 3; i++) {
        if (!_cards[i]) continue;
        lv_obj_clean(_cards[i]);
        if (i >= _result.nb_days) {
            lv_obj_t *lbl = lv_label_create(_cards[i]);
            lv_label_set_text(lbl, "---");
            lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
            lv_obj_center(lbl);
            continue;
        }

        const DayForecast &d = _result.days[i];

        auto mk_lbl = [&](const char *txt, const lv_font_t *fnt, uint32_t col,
                          lv_align_t align, int ox, int oy) {
            lv_obj_t *l = lv_label_create(_cards[i]);
            lv_label_set_text(l, txt);
            lv_obj_set_style_text_font(l, fnt, 0);
            lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
            lv_obj_align(l, align, ox, oy);
            return l;
        };

        mk_lbl(d.date, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_TOP_MID, 0, 6);
        mk_lbl(meteo_icon(d.code), &lv_font_montserrat_24, C_TXT, LV_ALIGN_TOP_MID, 0, 28);

        char cond[20]; snprintf(cond, sizeof(cond), "%s", meteo_label(d.code));
        lv_obj_t *lc = mk_lbl(cond, &lv_font_montserrat_14, C_TXT, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_set_style_text_align(lc, LV_TEXT_ALIGN_CENTER, 0);

        char temp[20]; snprintf(temp, sizeof(temp), "%d/%d C", d.tmin, d.tmax);
        mk_lbl(temp, &lv_font_montserrat_16, C_TXT, LV_ALIGN_BOTTOM_MID, 0, -22);

        char rain[16]; snprintf(rain, sizeof(rain), "Pluie: %d%%", d.rain10);
        mk_lbl(rain, &lv_font_montserrat_14, C_MUTED, LV_ALIGN_BOTTOM_MID, 0, -6);
    }
}

// ─── Callback LVGL (posté depuis la tâche HTTP) ───────────────────────────────
static void on_fetch_done(void *) {
    _fetch_running = false;
    if (!_scr) return;
    if (!_result.ok) {
        if (_lbl_status) lv_label_set_text(_lbl_status, _result.err);
        return;
    }
    if (_lbl_gps) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s (%.3f, %.3f)%s",
                 _result.city, _lat, _lon, _gps_ok ? "" : " *");
        lv_label_set_text(_lbl_gps, buf);
    }
    if (_lbl_status) lv_label_set_text(_lbl_status, "");
    refresh_cards();
}

// ─── Tâche FreeRTOS — fetch HTTP ──────────────────────────────────────────────
static void fetch_task(void *) {
    _result = {};

    if (!WiFi.isConnected()) {
        strncpy(_result.err, "WiFi non connecte", sizeof(_result.err));
        lv_async_call(on_fetch_done, nullptr);
        vTaskDelete(NULL); return;
    }

    if (g_meteo_api_key[0] == '\0') {
        strncpy(_result.err, "Cle API manquante (PWA Config)", sizeof(_result.err));
        lv_async_call(on_fetch_done, nullptr);
        vTaskDelete(NULL); return;
    }

    String url_loc = "https://api.meteo-concept.com/api/location/near?latlng=";
    url_loc += String(_lat, 5) + "," + String(_lon, 5) + "&token=" + String(g_meteo_api_key);

    HTTPClient http;
    http.setTimeout(6000);
    http.begin(url_loc);
    int rc = http.GET();
    if (rc != 200) {
        snprintf(_result.err, sizeof(_result.err), "Erreur loc %d", rc);
        http.end(); lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }

    DynamicJsonDocument doc_loc(2048);
    deserializeJson(doc_loc, http.getString());
    http.end();

    int insee = doc_loc["city"]["insee"] | 0;
    if (insee == 0) {
        strncpy(_result.err, "Ville introuvable", sizeof(_result.err));
        lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }

    strncpy(_result.city, doc_loc["city"]["nom"] | "?", sizeof(_result.city) - 1);

    String url_fc = "https://api.meteo-concept.com/api/forecast/daily?insee=";
    url_fc += String(insee) + "&token=" + String(g_meteo_api_key);

    HTTPClient http2;
    http2.setTimeout(8000);
    http2.begin(url_fc);
    rc = http2.GET();
    if (rc != 200) {
        snprintf(_result.err, sizeof(_result.err), "Erreur previsions %d", rc);
        http2.end(); lv_async_call(on_fetch_done, nullptr); vTaskDelete(NULL); return;
    }

    DynamicJsonDocument doc_fc(4096);
    deserializeJson(doc_fc, http2.getString());
    http2.end();

    JsonArray arr = doc_fc["forecast"].as<JsonArray>();
    _result.nb_days = 0;
    for (JsonObject day : arr) {
        if (_result.nb_days >= 3) break;
        DayForecast &d = _result.days[_result.nb_days];
        const char *dt = day["datetime"] | "0000-00-00";
        int yr = 0, mo = 0, da = 0;
        sscanf(dt, "%d-%d-%d", &yr, &mo, &da);
        if (mo < 3) { mo += 12; yr--; }
        int dow = (da + (13*(mo+1))/5 + yr + yr/4 - yr/100 + yr/400) % 7;
        static const int zmap[] = {6,0,1,2,3,4,5};
        int wd  = zmap[((dow % 7) + 7) % 7];
        int mod = (mo > 12) ? mo - 12 : mo;
        snprintf(d.date, sizeof(d.date), "%s %d %s", JOURS[wd], da,
                 (mod >= 1 && mod <= 12) ? MOIS[mod] : "?");
        d.tmin    = day["tmin"]      | 0;
        d.tmax    = day["tmax"]      | 0;
        d.code    = day["weather"]   | 0;
        d.rain10  = day["probarain"] | 0;
        _result.nb_days++;
    }

    _result.ok = true;
    Serial.printf("[APP/METEO] %d jours — %s (%.4f,%.4f)\n",
                  _result.nb_days, _result.city, _lat, _lon);
    lv_async_call(on_fetch_done, nullptr);
    vTaskDelete(NULL);
}

static void start_fetch() {
    if (_fetch_running) return;
    _fetch_running = true;
    if (_lbl_status) lv_label_set_text(_lbl_status, "Chargement...");
    xTaskCreatePinnedToCore(fetch_task, "meteo_http", 8192, nullptr, 1, nullptr, 0);
}

// ─── Retour safe : attend la fin de l'animation avant de supprimer _scr ───────
// Appelé aussi depuis meteo_app_stop() (long-press bouton physique via launcher)
static void do_close() {
    if (!_app_active) return;   // anti-double-appel
    _app_active    = false;
    _fetch_running = false;
    orchestrator_set_app(APP_LAUNCHER);
    Serial.println("[APP/METEO] Fermee");

    // Conserve le ptr de l'écran courant pour suppression différée
    _scr_to_delete = _scr;
    _scr           = nullptr;
    _lbl_gps       = nullptr;
    _lbl_status    = nullptr;
    for (int i = 0; i < 3; i++) _cards[i] = nullptr;

    // Lance l'animation de retour vers le launcher
    lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);

    // Attache anim_ready_cb pour supprimer l'ancien écran APRÈS l'animation
    lv_anim_t *anim = lv_screen_get_active_anim();
    if (anim) {
        lv_anim_set_ready_cb(anim, anim_ready_cb);
    } else {
        // Pas d'animation en cours (délai 0 ou réduite) → suppression immédiate
        if (_scr_to_delete) { lv_obj_del(_scr_to_delete); _scr_to_delete = nullptr; }
    }
}

static void back_cb(lv_event_t *) { do_close(); }

// ─── Création UI ──────────────────────────────────────────────────────────────
void meteo_app_start() {
    load_meteo_key();
    orchestrator_set_app(APP_METEO);
    _app_active = true;
    _last_fetch = 0;

    // Position : BLE → NVS → Paris
    double lat, lon;
    if (ble_mgr_get_gps(&lat, &lon)) {
        _lat = lat; _lon = lon; _gps_ok = true;
        nvs_save_pos(_lat, _lon);
    } else if (nvs_load_pos(lat, lon)) {
        _lat = lat; _lon = lon; _gps_ok = false;
    } else {
        _lat = 48.8566; _lon = 2.3522; _gps_ok = false;
    }

    _scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_btn_create(_scr);
    lv_obj_set_size(btn, 52, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lb, lv_color_hex(C_TXT), 0);
    lv_obj_center(lb);

    lv_obj_t *title = lv_label_create(_scr);
    lv_label_set_text(title, "Meteo");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    _lbl_gps = lv_label_create(_scr);
    char buf[48];
    if (_gps_ok)
        snprintf(buf, sizeof(buf), "GPS: %.3f, %.3f", _lat, _lon);
    else if (_lat != 48.8566 || _lon != 2.3522)
        snprintf(buf, sizeof(buf), "Derniere pos: %.3f, %.3f", _lat, _lon);
    else
        snprintf(buf, sizeof(buf), "Paris (fallback)");
    lv_label_set_text(_lbl_gps, buf);
    lv_obj_set_style_text_font(_lbl_gps, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_gps, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_gps, LV_ALIGN_TOP_MID, 0, 82);

    _lbl_status = lv_label_create(_scr);
    lv_label_set_text(_lbl_status, g_meteo_api_key[0] ? "" : "Config cle API dans la PWA");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_ERR), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 104);

    static const int CARD_W = 140, CARD_H = 180, GAP = 12;
    int total_w = 3 * CARD_W + 2 * GAP;
    int start_x = (480 - total_w) / 2;
    int card_y  = 140;

    for (int i = 0; i < 3; i++) {
        _cards[i] = lv_obj_create(_scr);
        lv_obj_set_size(_cards[i], CARD_W, CARD_H);
        lv_obj_set_pos(_cards[i], start_x + i * (CARD_W + GAP), card_y);
        lv_obj_set_style_bg_color(_cards[i], lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(_cards[i], LV_OPA_50, 0);
        lv_obj_set_style_border_color(_cards[i], lv_color_hex(C_TXT), 0);
        lv_obj_set_style_border_width(_cards[i], 1, 0);
        lv_obj_set_style_border_opa(_cards[i], LV_OPA_40, 0);
        lv_obj_set_style_radius(_cards[i], 14, 0);
        lv_obj_clear_flag(_cards[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *ph = lv_label_create(_cards[i]);
        lv_label_set_text(ph, "...");
        lv_obj_set_style_text_color(ph, lv_color_hex(C_MUTED), 0);
        lv_obj_center(ph);
    }

    lv_obj_t *hint = lv_label_create(_scr);
    lv_label_set_text(hint, _gps_ok ? "GPS BLE" : "Derniere pos NVS | Paris");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x1A1A1A), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    Serial.println("[APP/METEO] Ouverte");

    if (g_meteo_api_key[0]) start_fetch();
}

// ─── Tick ─────────────────────────────────────────────────────────────────────
void meteo_app_tick() {
    if (!_app_active || !_scr) return;
    if (g_meteo_api_key[0] == '\0') return;

    double lat, lon;
    if (ble_mgr_get_gps(&lat, &lon)) {
        if (!_gps_ok || lat != _lat || lon != _lon) {
            _lat = lat; _lon = lon; _gps_ok = true;
            nvs_save_pos(_lat, _lon);
            _last_fetch = 0;
        }
    }

    uint32_t now = millis();
    if (!_fetch_running && (_last_fetch == 0 || (now - _last_fetch) >= 600000UL)) {
        _last_fetch = now;
        start_fetch();
    }
}

// ─── Stop (appelé depuis stop_current_and_return du launcher) ─────────────────
void meteo_app_stop() {
    do_close();   // logique centralisée — ui_launcher_return() sera appelé ensuite
}
