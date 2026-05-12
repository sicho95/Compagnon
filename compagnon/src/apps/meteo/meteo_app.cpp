/**
 * Afficher prévisions météo J+3 via api.meteo-concept.com.
 * Position GPS : BLE téléphone → dernière position NVS → Paris (fallback final).
 */
#include "meteo_app.h"
#include "../../net/ble_mgr.h"
#include "../../system/orchestrator.h"
#include "../../config/nvs_config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <time.h>

// ─── Constantes ───────────────────────────────────────────────────────────────
static const char *API_URL =
    "https://api.meteo-concept.com/api/forecast/daily?token=%s&latlng=%s,%s";
static const char *DAYS_FR[] = { "Auj.", "Dem.", "J+2", "J+3" };
static const int   FORECAST_DAYS = 4;
static const unsigned long REFRESH_MS = 10UL * 60 * 1000;  // 10 min

// Coordonnées Paris (fallback final)
static const double PARIS_LAT = 48.8566;
static const double PARIS_LON =  2.3522;

// ─── État ─────────────────────────────────────────────────────────────────────
static lv_obj_t   *_screen     = nullptr;
static lv_obj_t   *_cards[4]   = {};
static lv_obj_t   *_day_lbl[4] = {};
static lv_obj_t   *_tmin_lbl[4]= {};
static lv_obj_t   *_tmax_lbl[4]= {};
static lv_obj_t   *_rain_lbl[4]= {};
static lv_obj_t   *_status_lbl = nullptr;
static lv_timer_t *_timer      = nullptr;
static unsigned long _last_fetch = 0;
static bool _open = false;
static char _api_key[128] = "";
static double _lat = PARIS_LAT;
static double _lon = PARIS_LON;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static void set_status(const char *msg) {
    if (_status_lbl) lv_label_set_text(_status_lbl, msg);
}

static void update_card(int idx, int tmin, int tmax, int rain, int pluie) {
    if (idx < 0 || idx >= FORECAST_DAYS) return;
    char buf[16];

    snprintf(buf, sizeof(buf), "%d°", tmin);
    if (_tmin_lbl[idx]) lv_label_set_text(_tmin_lbl[idx], buf);

    snprintf(buf, sizeof(buf), "%d°", tmax);
    if (_tmax_lbl[idx]) lv_label_set_text(_tmax_lbl[idx], buf);

    snprintf(buf, sizeof(buf), "%d%%", pluie);
    if (_rain_lbl[idx]) lv_label_set_text(_rain_lbl[idx], buf);
}

// ─── Résolution position ──────────────────────────────────────────────────────
static void resolve_position() {
    double lat, lon;
    // 1. BLE GPS (téléphone)
    if (ble_mgr_get_gps(&lat, &lon)) {
        _lat = lat; _lon = lon;
        nvs_set_double("last_lat", lat);
        nvs_set_double("last_lon", lon);
        Serial.printf("[APP/METEO] GPS BLE: %.5f, %.5f\n", lat, lon);
        return;
    }
    // 2. Dernière position NVS
    lat = nvs_get_double("last_lat", 999.0);
    lon = nvs_get_double("last_lon", 999.0);
    if (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
        _lat = lat; _lon = lon;
        Serial.printf("[APP/METEO] GPS NVS: %.5f, %.5f\n", lat, lon);
        return;
    }
    // 3. Paris
    _lat = PARIS_LAT; _lon = PARIS_LON;
    Serial.println("[APP/METEO] GPS fallback Paris");
}

// ─── Fetch HTTP ───────────────────────────────────────────────────────────────
static void fetch_meteo() {
    if (strlen(_api_key) == 0) {
        set_status("Clé API manquante");
        return;
    }
    resolve_position();
    set_status("Chargement...");

    char lat_s[16], lon_s[16];
    snprintf(lat_s, sizeof(lat_s), "%.5f", _lat);
    snprintf(lon_s, sizeof(lon_s), "%.5f", _lon);

    char url[512];
    snprintf(url, sizeof(url), API_URL, _api_key, lat_s, lon_s);

    HTTPClient http;
    http.begin(url);
    int code = http.GET();
    if (code != 200) {
        char err[32];
        snprintf(err, sizeof(err), "Erreur HTTP %d", code);
        set_status(err);
        http.end();
        return;
    }

    String body = http.getString();
    http.end();

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, body)) {
        set_status("JSON invalide");
        return;
    }

    JsonArray forecast = doc["forecast"];
    int count = 0;
    for (JsonObject day : forecast) {
        if (count >= FORECAST_DAYS) break;
        int tmin  = day["tmin"]  | 0;
        int tmax  = day["tmax"]  | 0;
        int rain  = day["rr10"]  | 0;
        int pluie = day["probarain"] | 0;
        update_card(count, tmin, tmax, rain, pluie);
        count++;
    }
    set_status("OK");
    _last_fetch = millis();
}

// ─── Timer ────────────────────────────────────────────────────────────────────
static void on_timer(lv_timer_t *) {
    if (!_open) return;
    if (millis() - _last_fetch >= REFRESH_MS) fetch_meteo();
}

// ─── Bouton retour ────────────────────────────────────────────────────────────
static void on_back(lv_event_t *) {
    meteo_app_stop();
    orchestrator_launch_home();
}

// ─── Build UI ─────────────────────────────────────────────────────────────────
static void build_ui() {
    _screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(_screen);
    lv_label_set_text(title, LV_SYMBOL_CLOUD " Météo");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *btn_back = lv_btn_create(_screen);
    lv_obj_set_size(btn_back, 60, 36);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn_back);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT);
    lv_obj_center(lbl);

    int card_w = 90, card_h = 200;
    int start_x = 15, start_y = 65, gap = 8;

    for (int i = 0; i < FORECAST_DAYS; i++) {
        int x = start_x + i * (card_w + gap);

        lv_obj_t *card = lv_obj_create(_screen);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, x, start_y);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x141428), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        _cards[i] = card;

        _day_lbl[i] = lv_label_create(card);
        lv_label_set_text(_day_lbl[i], DAYS_FR[i]);
        lv_obj_set_style_text_color(_day_lbl[i], lv_color_hex(0xaaaaff), 0);
        lv_obj_set_style_text_font(_day_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_align(_day_lbl[i], LV_ALIGN_TOP_MID, 0, 0);

        _tmax_lbl[i] = lv_label_create(card);
        lv_label_set_text(_tmax_lbl[i], "--°");
        lv_obj_set_style_text_color(_tmax_lbl[i], lv_color_hex(0xff6b35), 0);
        lv_obj_set_style_text_font(_tmax_lbl[i], &lv_font_montserrat_18, 0);
        lv_obj_align(_tmax_lbl[i], LV_ALIGN_CENTER, 0, -20);

        _tmin_lbl[i] = lv_label_create(card);
        lv_label_set_text(_tmin_lbl[i], "--°");
        lv_obj_set_style_text_color(_tmin_lbl[i], lv_color_hex(0x6be4ff), 0);
        lv_obj_set_style_text_font(_tmin_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_align(_tmin_lbl[i], LV_ALIGN_CENTER, 0, 10);

        _rain_lbl[i] = lv_label_create(card);
        lv_label_set_text(_rain_lbl[i], "--%");
        lv_obj_set_style_text_color(_rain_lbl[i], lv_color_hex(0x55aaff), 0);
        lv_obj_set_style_text_font(_rain_lbl[i], &lv_font_montserrat_12, 0);
        lv_obj_align(_rain_lbl[i], LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    _status_lbl = lv_label_create(_screen);
    lv_label_set_text(_status_lbl, "");
    lv_obj_set_style_text_color(_status_lbl, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(_status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(_status_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ─── do_close ─────────────────────────────────────────────────────────────────
static void do_close() {
    _open = false;
    if (_timer) { lv_timer_del(_timer); _timer = nullptr; }
    if (_screen) { lv_obj_del(_screen); _screen = nullptr; }
    for (int i = 0; i < FORECAST_DAYS; i++) {
        _cards[i] = _day_lbl[i] = _tmin_lbl[i] = _tmax_lbl[i] = _rain_lbl[i] = nullptr;
    }
    _status_lbl = nullptr;
}

// ─── API publique ─────────────────────────────────────────────────────────────
void meteo_app_start() {
    if (_open) return;

    String key = nvs_get_str("meteo_key", "");
    if (key.length() == 0)
        Serial.println("[NVS] cle absente : 'meteo_key'");
    strlcpy(_api_key, key.c_str(), sizeof(_api_key));

    build_ui();
    lv_scr_load(_screen);
    _open = true;
    _last_fetch = 0;

    _timer = lv_timer_create(on_timer, 5000, NULL);
    fetch_meteo();
    Serial.println("[APP/METEO] Ouverte");
}

void meteo_app_stop() {
    do_close();
}
