/**
 * App Météo — isolation mémoire : struct MeteoState allouée à l'ouverture.
 * Quand fermée : 4 bytes (pointeur nullptr). Quand ouverte : ~1KB.
 * Cache SD : /compagnon/meteo/cache.json (affichage immédiat sans attente réseau).
 */
#include "meteo_app.h"
#include "../../net/ble_mgr.h"
#include "../../net/net_utils.h"
#include "../../system/orchestrator.h"
#include "../../system/sd_mgr.h"
#include "../../config/nvs_config.h"
#include "../../config/ui_config.h"
#include "../../ui/launcher.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <WiFi.h>

#define SD_CACHE_PATH "/compagnon/meteo/cache.json"

static const char *API_URL =
    "https://api.meteo-concept.com/api/forecast/daily?token=%s&latlng=%s,%s";
static const char *DAYS_FR[]   = { "Auj.", "Dem.", "J+2", "J+3" };
static const int   FORECAST_DAYS = 4;
static const unsigned long REFRESH_MS = 10UL * 60 * 1000;

static const double PARIS_LAT = 48.8566;
static const double PARIS_LON =  2.3522;

// ─── Tout l'état de l'app dans une struct ─────────────────────────────────────
struct MeteoState {
    // UI
    lv_obj_t   *screen     = nullptr;
    lv_obj_t   *cards[4]   = {};
    lv_obj_t   *day_lbl[4] = {};
    lv_obj_t   *tmin_lbl[4]= {};
    lv_obj_t   *tmax_lbl[4]= {};
    lv_obj_t   *rain_lbl[4]= {};
    lv_obj_t   *status_lbl = nullptr;
    lv_timer_t *timer      = nullptr;
    // Fetch
    volatile bool fetch_running = false;
    unsigned long last_fetch    = 0;
    // Config
    char   api_key[128] = {};
    double lat          = 48.8566;
    double lon          =  2.3522;
    // Résultat fetch (task→LVGL via lv_async_call)
    struct FetchResult {
        bool ok = false;
        char err[48] = {};
        struct Day { int tmin, tmax, rain, pluie; } days[4];
        int count = 0;
    } fres;
};

// Le seul static autorisé : le pointeur (4 bytes quand app fermée)
static MeteoState *_s = nullptr;

bool meteo_app_is_running() { return _s != nullptr; }

// ─── Helpers ─────────────────────────────────────────────────────────────────
static void set_status(const char *msg) {
    if (_s && _s->status_lbl) lv_label_set_text(_s->status_lbl, msg);
}

static void update_card(int idx) {
    if (!_s || idx < 0 || idx >= FORECAST_DAYS) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d°", _s->fres.days[idx].tmin);
    if (_s->tmin_lbl[idx]) lv_label_set_text(_s->tmin_lbl[idx], buf);
    snprintf(buf, sizeof(buf), "%d°", _s->fres.days[idx].tmax);
    if (_s->tmax_lbl[idx]) lv_label_set_text(_s->tmax_lbl[idx], buf);
    snprintf(buf, sizeof(buf), "%d%%", _s->fres.days[idx].pluie);
    if (_s->rain_lbl[idx]) lv_label_set_text(_s->rain_lbl[idx], buf);
}

static void resolve_position() {
    double lat, lon;
    if (ble_mgr_get_gps(&lat, &lon)) {
        _s->lat = lat; _s->lon = lon;
        nvs_set_double("last_lat", lat);
        nvs_set_double("last_lon", lon);
        return;
    }
    lat = nvs_get_double("last_lat", 999.0);
    lon = nvs_get_double("last_lon", 999.0);
    if (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
        _s->lat = lat; _s->lon = lon;
        return;
    }
    _s->lat = PARIS_LAT; _s->lon = PARIS_LON;
}

static void parse_and_display(const String& json_str) {
    if (!_s || json_str.isEmpty()) return;
    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, json_str)) return;
    JsonArray forecast = doc["forecast"];
    int i = 0;
    for (JsonObject day : forecast) {
        if (i >= FORECAST_DAYS) break;
        _s->fres.days[i] = { day["tmin"]|0, day["tmax"]|0,
                             day["rr10"]|0, day["probarain"]|0 };
        update_card(i);
        i++;
    }
}

// ─── Callback LVGL (résultat fetch) ──────────────────────────────────────────
static void on_fetch_done(void*) {
    if (!_s) return;
    _s->fetch_running = false;
    if (!_s->fres.ok) {
        set_status(_s->fres.err);
        return;
    }
    // Sauvegarder sur SD
    // (le JSON brut est déjà sauvegardé dans fetch_task, pas besoin de le refaire ici)
    for (int i = 0; i < _s->fres.count; i++) update_card(i);
    set_status("OK");
    _s->last_fetch = millis();
}

// ─── FreeRTOS task réseau (Core 0, 24KB stack) ───────────────────────────────
static void fetch_task(void*) {
    if (!_s) { vTaskDelete(NULL); return; }
    _s->fres = {};

    if (!WiFi.isConnected()) {
        strlcpy(_s->fres.err, "WiFi non connecte", sizeof(_s->fres.err));
        lv_async_call(on_fetch_done, nullptr);
        vTaskDelete(NULL); return;
    }
    if (_s->api_key[0] == '\0') {
        strlcpy(_s->fres.err, "Cle API manquante", sizeof(_s->fres.err));
        lv_async_call(on_fetch_done, nullptr);
        vTaskDelete(NULL); return;
    }

    resolve_position();

    char lat_s[16], lon_s[16], url[512];
    snprintf(lat_s, sizeof(lat_s), "%.5f", _s->lat);
    snprintf(lon_s, sizeof(lon_s), "%.5f", _s->lon);
    snprintf(url,   sizeof(url),   API_URL, _s->api_key, lat_s, lon_s);

    int code = 0;
    String body = https_get(url, &code);

    Serial.printf("[METEO] fetch_task stack min: %d bytes\n",
                  uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));

    if (code != 200) {
        snprintf(_s->fres.err, sizeof(_s->fres.err), "Erreur HTTP %d", code);
        lv_async_call(on_fetch_done, nullptr);
        vTaskDelete(NULL); return;
    }

    // Sauvegarder sur SD (cache pour prochain démarrage)
    if (sd_mgr_available()) {
        sd_write_json(SD_CACHE_PATH, body.c_str());
    }

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, body)) {
        strlcpy(_s->fres.err, "JSON invalide", sizeof(_s->fres.err));
        lv_async_call(on_fetch_done, nullptr);
        vTaskDelete(NULL); return;
    }

    JsonArray forecast = doc["forecast"];
    _s->fres.count = 0;
    for (JsonObject day : forecast) {
        if (_s->fres.count >= FORECAST_DAYS) break;
        _s->fres.days[_s->fres.count] = {
            day["tmin"]      | 0,
            day["tmax"]      | 0,
            day["rr10"]      | 0,
            day["probarain"] | 0
        };
        _s->fres.count++;
    }
    _s->fres.ok = true;
    lv_async_call(on_fetch_done, nullptr);
    vTaskDelete(NULL);
}

static void start_fetch() {
    if (!_s || _s->fetch_running) return;
    _s->fetch_running = true;
    xTaskCreatePinnedToCore(fetch_task, "meteo_fetch", 24576, nullptr, 1, nullptr, 0);
}

// ─── Timer ───────────────────────────────────────────────────────────────────
static void on_timer(lv_timer_t*) {
    if (!_s) return;
    if (millis() - _s->last_fetch >= REFRESH_MS) start_fetch();
}

static void on_back(lv_event_t*) { meteo_app_stop(); }

// ─── Build UI ─────────────────────────────────────────────────────────────────
static void build_ui() {
    _s->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_s->screen, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_bg_opa(_s->screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(_s->screen);
    lv_label_set_text(title, LV_SYMBOL_UP " Météo");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 0, APP_Y + 6);
    lv_obj_set_width(title, SCREEN_W);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *btn_back = lv_btn_create(_s->screen);
    lv_obj_set_size(btn_back, 60, 36);
    lv_obj_set_pos(btn_back, 8, APP_Y);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn_back);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT);
    lv_obj_center(lbl);

    int card_w = 90, card_h = 200;
    int start_x = 15, start_y = APP_Y + 48, gap = 8;

    for (int i = 0; i < FORECAST_DAYS; i++) {
        int x = start_x + i * (card_w + gap);
        lv_obj_t *card = lv_obj_create(_s->screen);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, x, start_y);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x141428), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 8, 0);
        _s->cards[i] = card;

        _s->day_lbl[i] = lv_label_create(card);
        lv_label_set_text(_s->day_lbl[i], DAYS_FR[i]);
        lv_obj_set_style_text_color(_s->day_lbl[i], lv_color_hex(0xaaaaff), 0);
        lv_obj_set_style_text_font(_s->day_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_align(_s->day_lbl[i], LV_ALIGN_TOP_MID, 0, 0);

        _s->tmax_lbl[i] = lv_label_create(card);
        lv_label_set_text(_s->tmax_lbl[i], "--°");
        lv_obj_set_style_text_color(_s->tmax_lbl[i], lv_color_hex(0xff6b35), 0);
        lv_obj_set_style_text_font(_s->tmax_lbl[i], &lv_font_montserrat_18, 0);
        lv_obj_align(_s->tmax_lbl[i], LV_ALIGN_CENTER, 0, -20);

        _s->tmin_lbl[i] = lv_label_create(card);
        lv_label_set_text(_s->tmin_lbl[i], "--°");
        lv_obj_set_style_text_color(_s->tmin_lbl[i], lv_color_hex(0x6be4ff), 0);
        lv_obj_set_style_text_font(_s->tmin_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_align(_s->tmin_lbl[i], LV_ALIGN_CENTER, 0, 10);

        _s->rain_lbl[i] = lv_label_create(card);
        lv_label_set_text(_s->rain_lbl[i], "--%");
        lv_obj_set_style_text_color(_s->rain_lbl[i], lv_color_hex(0x55aaff), 0);
        lv_obj_set_style_text_font(_s->rain_lbl[i], &lv_font_montserrat_12, 0);
        lv_obj_align(_s->rain_lbl[i], LV_ALIGN_BOTTOM_MID, 0, -4);
    }

    _s->status_lbl = lv_label_create(_s->screen);
    lv_label_set_text(_s->status_lbl, "");
    lv_obj_set_style_text_color(_s->status_lbl, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(_s->status_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(_s->status_lbl, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ─── API publique ─────────────────────────────────────────────────────────────
void meteo_app_start() {
    if (_s) return;  // déjà ouverte
    _s = new MeteoState();
    orchestrator_set_app(APP_METEO);

    String key = nvs_get_str("meteo_key", "");
    if (key.isEmpty()) Serial.println("[METEO] cle NVS absente: 'meteo_key'");
    strlcpy(_s->api_key, key.c_str(), sizeof(_s->api_key));

    build_ui();
    lv_scr_load(_s->screen);

    // Affichage immédiat depuis cache SD
    if (sd_mgr_available()) {
        String cached = sd_read_json(SD_CACHE_PATH);
        if (!cached.isEmpty()) {
            parse_and_display(cached);
            set_status("Cache SD");
        }
    }

    _s->timer = lv_timer_create(on_timer, 5000, NULL);
    start_fetch();
    Serial.println("[METEO] Ouverte — RAM allouée");
}

void meteo_app_stop() {
    if (!_s) return;
    _s->fetch_running = false;
    if (_s->timer)  { lv_timer_del(_s->timer);  _s->timer  = nullptr; }
    // lv_obj_del libère tout le sous-arbre LVGL d'un coup
    if (_s->screen) { lv_obj_del(_s->screen);   _s->screen = nullptr; }
    delete _s;
    _s = nullptr;
    orchestrator_set_app(APP_LAUNCHER);
    ui_launcher_return();
    Serial.println("[METEO] Fermée — RAM libérée");
}
