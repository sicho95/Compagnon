/**
 * App Radars — AUTONOME
 *
 * Sources de données (priorité décroissante) :
 *   1. GPS matériel intégré si disponible (gps.h)
 *   2. Position GPS + vitesse envoyées par le téléphone via BLE
 *
 * Téléchargement des radars :
 *   - Via WiFi si connecté  → API Lufop HTTPS
 *   - Aucun réseau          → affiche les radars déjà en cache
 *
 * Logique d'alerte (100 % dans l'ESP32) :
 *   - Distance au prochain radar calculée par Haversine
 *   - Vitesse : priorité BLE téléphone, sinon 0 km/h (pas d'alerte vitesse)
 *   - Seuils d'alerte : 500 m / 300 m / 150 m
 *   - Sons joués sur le HP ES8311 via ledcWriteTone (API arduino-esp32 ≥3.x)
 *
 * Aucune dépendance à l'appli Nestor / sicho95.github.io
 *
 * NOTE API audio arduino-esp32 3.x :
 *   ledcSetup() + ledcAttachPin()  →  ledcAttach(pin, freq, resolution)
 *   ledcDetachPin()                →  ledcDetach(pin)
 *   ledcWriteTone() reste disponible et inchangé
 */
#include "radar_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include "../../net/ble_mgr.h"
#include "../../gps.h"
#include "../../config/pin_config.h"
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <math.h>

// ─── Thème ────────────────────────────────────────────────────────────────────
#define C_BG      0x060614
#define C_CARD    0x0C0C22
#define C_TITLE   0x7EB8F7
#define C_MUTED   0x334466
#define C_RED     0xEE3322
#define C_ORANGE  0xFF8800
#define C_GREEN   0x44AA55
#define C_NEUTRAL 0xBBCCDD
#define C_STATUS  0x4488AA
#define C_WARN    0xFFDD00

// ─── Paramètres ───────────────────────────────────────────────────────────────
#define MAX_RADARS      20
#define FETCH_MS        300000UL   // 5 min entre deux téléchargements
#define MOVE_THRESH_M   50.0f      // re-fetch si déplacement > 50 m

// Seuils d'alerte (mètres) — 3 niveaux
#define ALERT_FAR_M     500
#define ALERT_MID_M     300
#define ALERT_NEAR_M    150

// Audio — API arduino-esp32 3.x : ledcAttach(pin, freq, resolution)
// Plus de canal explicite : le canal est géré en interne par le driver
#define AUDIO_LEDC_FREQ  880        // fréquence d'attache initiale (Hz)
#define AUDIO_LEDC_RES   8          // 8 bits de résolution
#define AUDIO_DUTY_50    128        // 50 % duty = carré pur

// Fréquences et durées des bips
#define BEEP_FAR_HZ     880
#define BEEP_MID_HZ     1320
#define BEEP_NEAR_HZ    2200
#define BEEP_SHORT_MS   100
#define BEEP_LONG_MS    300

// ─── Radar ────────────────────────────────────────────────────────────────────
struct Radar {
    float lat, lon;
    int   speed;       // limite en km/h (0 = inconnue)
    char  type[16];
    float dist_m;
};

static Radar _radars[MAX_RADARS];
static int   _nb_radars = 0;

// ─── État UI ─────────────────────────────────────────────────────────────────
static lv_obj_t *_scr        = nullptr;
static lv_obj_t *_lbl_gps    = nullptr;
static lv_obj_t *_lbl_speed  = nullptr;
static lv_obj_t *_lbl_status = nullptr;
static lv_obj_t *_lbl_alert  = nullptr;
static lv_obj_t *_list       = nullptr;

static bool     _app_active    = false;
static uint32_t _last_fetch    = 0;
static double   _lat = 0.0,  _lon = 0.0;
static bool     _has_gps       = false;
static float    _speed_kmh     = 0.0f;
static volatile bool _fetch_running = false;

// ─── Alerte sonore ────────────────────────────────────────────────────────────
static int      _alert_level   = 0;
static uint32_t _alert_last_ms = 0;
static bool     _audio_init    = false;

static void audio_init() {
    if (_audio_init) return;
    // Activer l'ampli classe-D
    pinMode(PA, OUTPUT);
    digitalWrite(PA, HIGH);
    // API arduino-esp32 ≥3.x : ledcAttach(pin, freq_hz, resolution_bits)
    // Remplace ledcSetup() + ledcAttachPin() de l'ancienne API
    ledcAttach(PIN_ES8311_DOUT, AUDIO_LEDC_FREQ, AUDIO_LEDC_RES);
    ledcWrite(PIN_ES8311_DOUT, 0);  // silence
    _audio_init = true;
    Serial.println("[RADAR] Audio initialisé");
}

static void audio_deinit() {
    if (!_audio_init) return;
    // API arduino-esp32 ≥3.x : ledcDetach(pin)
    // Remplace ledcDetachPin() de l'ancienne API
    ledcDetach(PIN_ES8311_DOUT);
    digitalWrite(PA, LOW);
    _audio_init = false;
}

// Jouer un bip bloquant depuis la tâche dédiée (jamais depuis le contexte LVGL)
// API 3.x : ledcWriteTone(pin, freq) + ledcWrite(pin, duty)
static void beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (!_audio_init) return;
    digitalWrite(PA, HIGH);
    ledcWriteTone(PIN_ES8311_DOUT, freq_hz);
    ledcWrite(PIN_ES8311_DOUT, AUDIO_DUTY_50);
    delay(duration_ms);
    ledcWrite(PIN_ES8311_DOUT, 0);
    ledcWriteTone(PIN_ES8311_DOUT, 0);
}

static void play_alert_pattern(int level) {
    if (!_audio_init) return;
    switch (level) {
        case 1:  // 1 bip grave — 500 m
            beep(BEEP_FAR_HZ, BEEP_SHORT_MS);
            break;
        case 2:  // 2 bips médium — 300 m
            beep(BEEP_MID_HZ, BEEP_SHORT_MS);
            delay(80);
            beep(BEEP_MID_HZ, BEEP_SHORT_MS);
            break;
        case 3:  // 3 bips aigus — 150 m
            beep(BEEP_NEAR_HZ, BEEP_SHORT_MS);
            delay(50);
            beep(BEEP_NEAR_HZ, BEEP_SHORT_MS);
            delay(50);
            beep(BEEP_NEAR_HZ, BEEP_LONG_MS);
            break;
        default: break;
    }
}

// ─── Tâche FreeRTOS dédiée aux bips (évite de bloquer LVGL) ─────────────────
static volatile int   _beep_request = 0;
static TaskHandle_t   _beep_task_h  = nullptr;

static void beep_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int lvl = _beep_request;
        _beep_request = 0;
        if (lvl > 0) play_alert_pattern(lvl);
    }
}

static void trigger_beep(int level) {
    if (!_beep_task_h) return;
    _beep_request = level;
    xTaskNotifyGive(_beep_task_h);
}

// ─── Haversine (mètres) ───────────────────────────────────────────────────────
static float haversine(double la1, double lo1, double la2, double lo2) {
    const double R = 6371e3;
    double dlat = (la2 - la1) * M_PI / 180.0;
    double dlon = (lo2 - lo1) * M_PI / 180.0;
    double a = sin(dlat/2)*sin(dlat/2)
             + cos(la1*M_PI/180.0)*cos(la2*M_PI/180.0)
               *sin(dlon/2)*sin(dlon/2);
    return (float)(R * 2.0 * atan2(sqrt(a), sqrt(1.0-a)));
}

static uint32_t dist_color(float d) {
    if (d < 500.f)  return C_RED;
    if (d < 2000.f) return C_ORANGE;
    return C_GREEN;
}

static void fmt_dist(char *buf, size_t sz, float d) {
    if (d < 1000.f) snprintf(buf, sz, "%.0f m",  d);
    else            snprintf(buf, sz, "%.1f km", d / 1000.f);
}

static void sort_radars() {
    for (int i = 1; i < _nb_radars; i++) {
        Radar key = _radars[i]; int j = i - 1;
        while (j >= 0 && _radars[j].dist_m > key.dist_m) {
            _radars[j+1] = _radars[j]; j--;
        }
        _radars[j+1] = key;
    }
}

// ─── Logique d'alerte ─────────────────────────────────────────────────────────
static void update_alert() {
    if (_nb_radars == 0 || !_has_gps) {
        if (_alert_level != 0) {
            _alert_level = 0;
            if (_lbl_alert) lv_label_set_text(_lbl_alert, "");
        }
        return;
    }

    float nearest    = _radars[0].dist_m;
    int   speed_limit = _radars[0].speed;

    int new_level = 0;
    if      (nearest < ALERT_NEAR_M) new_level = 3;
    else if (nearest < ALERT_MID_M)  new_level = 2;
    else if (nearest < ALERT_FAR_M)  new_level = 1;

    static const uint32_t BEEP_INTERVAL[] = { 0, 4000, 2000, 800 };
    uint32_t now = millis();

    if (new_level > 0) {
        bool level_changed    = (new_level != _alert_level);
        bool interval_elapsed = (now - _alert_last_ms) >= BEEP_INTERVAL[new_level];

        if (level_changed || interval_elapsed) {
            _alert_level   = new_level;
            _alert_last_ms = now;
            trigger_beep(new_level);

            if (_lbl_alert) {
                const char *level_str[]   = { "", "⚠ Radar 500m", "⚠⚠ Radar 300m", "🚨 RADAR 150m" };
                uint32_t    alert_colors[] = { C_BG, C_GREEN, C_ORANGE, C_RED };
                lv_label_set_text(_lbl_alert, level_str[new_level]);
                lv_obj_set_style_text_color(_lbl_alert,
                    lv_color_hex(alert_colors[new_level]), 0);
            }
        }
    } else {
        if (_alert_level != 0) {
            _alert_level = 0;
            if (_lbl_alert) lv_label_set_text(_lbl_alert, "");
        }
    }

    if (_lbl_speed) {
        char spd_buf[48];
        if (speed_limit > 0 && _speed_kmh > 0.0f) {
            bool over = (_speed_kmh > (float)speed_limit + 5.0f);
            snprintf(spd_buf, sizeof(spd_buf), "%.0f / %d km/h", _speed_kmh, speed_limit);
            lv_obj_set_style_text_color(_lbl_speed,
                lv_color_hex(over ? C_RED : C_GREEN), 0);
        } else if (_speed_kmh > 0.0f) {
            snprintf(spd_buf, sizeof(spd_buf), "%.0f km/h", _speed_kmh);
            lv_obj_set_style_text_color(_lbl_speed, lv_color_hex(C_NEUTRAL), 0);
        } else {
            snprintf(spd_buf, sizeof(spd_buf), "Vitesse: N/A");
            lv_obj_set_style_text_color(_lbl_speed, lv_color_hex(C_MUTED), 0);
        }
        lv_label_set_text(_lbl_speed, spd_buf);
    }
}

// ─── Rafraîchir la liste LVGL ────────────────────────────────────────────────
static void refresh_list() {
    if (!_list) return;
    lv_obj_clean(_list);

    if (_nb_radars == 0) {
        lv_obj_t *empty = lv_label_create(_list);
        lv_label_set_text(empty, "Aucun radar dans les 20 km");
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(C_MUTED), 0);
        lv_obj_center(empty);
        return;
    }

    for (int i = 0; i < _nb_radars; i++) {
        const Radar &r = _radars[i];
        uint32_t col   = dist_color(r.dist_m);

        lv_obj_t *row = lv_obj_create(_list);
        lv_obj_set_size(row, LV_PCT(100), 58);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(col), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        char dbuf[16]; fmt_dist(dbuf, sizeof(dbuf), r.dist_m);
        lv_obj_t *ld = lv_label_create(row);
        lv_label_set_text(ld, dbuf);
        lv_obj_set_style_text_font(ld, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(ld, lv_color_hex(col), 0);
        lv_obj_align(ld, LV_ALIGN_LEFT_MID, 0, -8);

        lv_obj_t *lt = lv_label_create(row);
        lv_label_set_text(lt, r.type);
        lv_obj_set_style_text_font(lt, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lt, lv_color_hex(C_MUTED), 0);
        lv_obj_align(lt, LV_ALIGN_LEFT_MID, 0, 12);

        if (r.speed > 0) {
            char sbuf[16]; snprintf(sbuf, sizeof(sbuf), "%d km/h", r.speed);
            lv_obj_t *ls = lv_label_create(row);
            lv_label_set_text(ls, sbuf);
            lv_obj_set_style_text_font(ls, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(ls, lv_color_hex(C_NEUTRAL), 0);
            lv_obj_align(ls, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
}

// ─── Callback LVGL (depuis tâche HTTP) ───────────────────────────────────────
static void on_radar_done(void *) {
    _fetch_running = false;
    if (!_scr) return;
    refresh_list();
    update_alert();
    if (_lbl_status) {
        char buf[40];
        snprintf(buf, sizeof(buf), "%d radar(s) — %s",
                 _nb_radars, WiFi.isConnected() ? "WiFi" : "cache");
        lv_label_set_text(_lbl_status, buf);
    }
}

// ─── Tâche FreeRTOS — fetch Lufop ────────────────────────────────────────────
static void fetch_task(void *) {
    if (!WiFi.isConnected()) {
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "Pas de WiFi — cache utilisé");
        }, nullptr);
        vTaskDelete(NULL); return;
    }

    char url[200];
    snprintf(url, sizeof(url),
        "https://api.lufop.net/api?format=json&nbr=50&q=%.5f,%.5f&m=20&pays=fr",
        _lat, _lon);

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    int rc = http.GET();

    if (rc != 200) {
        char *msg = (char *)malloc(64);
        if (msg) snprintf(msg, 64, "Lufop erreur %d", rc);
        http.end();
        lv_async_call([](void *p) {
            _fetch_running = false;
            if (_lbl_status && p) lv_label_set_text(_lbl_status, (char *)p);
            free(p);
        }, msg);
        vTaskDelete(NULL); return;
    }

    DynamicJsonDocument doc(16384);
    DeserializationError derr = deserializeJson(doc, http.getString());
    http.end();

    if (derr) {
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "JSON invalide");
        }, nullptr);
        vTaskDelete(NULL); return;
    }

    JsonArray arr;
    if (doc.containsKey("radars"))        arr = doc["radars"].as<JsonArray>();
    else if (doc.containsKey("features")) arr = doc["features"].as<JsonArray>();

    _nb_radars = 0;
    for (JsonObject r : arr) {
        if (_nb_radars >= MAX_RADARS) break;
        Radar &rd = _radars[_nb_radars];
        if (r.containsKey("lat")) {
            rd.lat = r["lat"] | 0.0f;
            rd.lon = r["lng"] | r["lon"] | 0.0f;
        } else if (r.containsKey("geometry")) {
            rd.lat = r["geometry"]["coordinates"][1] | 0.0f;
            rd.lon = r["geometry"]["coordinates"][0] | 0.0f;
        } else { continue; }
        if (rd.lat == 0.0f && rd.lon == 0.0f) continue;
        rd.speed = r["vitesse"] | r["speed"] | r["properties"]["speed"] | 0;
        const char *t = r["type"] | r["properties"]["type"] | "fixe";
        strncpy(rd.type, t, sizeof(rd.type)-1);
        rd.type[sizeof(rd.type)-1] = '\0';
        rd.dist_m = haversine(_lat, _lon, rd.lat, rd.lon);
        _nb_radars++;
    }
    sort_radars();
    Serial.printf("[APP/RADARS] %d radars (%.4f,%.4f)\n", _nb_radars, _lat, _lon);
    lv_async_call(on_radar_done, nullptr);
    vTaskDelete(NULL);
}

static void start_fetch() {
    if (_fetch_running) return;
    _fetch_running = true;
    _last_fetch    = millis();
    if (_lbl_status) lv_label_set_text(_lbl_status, "Chargement...");
    xTaskCreatePinnedToCore(fetch_task, "radar_http", 8192, nullptr, 1, nullptr, 0);
}

// ─── Close ────────────────────────────────────────────────────────────────────
static void do_close() {
    if (!_app_active) return;
    _app_active    = false;
    _fetch_running = false;
    _alert_level   = 0;

    audio_deinit();
    if (_beep_task_h) {
        vTaskDelete(_beep_task_h);
        _beep_task_h = nullptr;
    }

    orchestrator_set_app(APP_LAUNCHER);
    Serial.println("[APP/RADARS] Fermée");

    lv_obj_t *scr_old = _scr;
    _scr        = nullptr;
    _list       = nullptr;
    _lbl_gps    = nullptr;
    _lbl_speed  = nullptr;
    _lbl_status = nullptr;
    _lbl_alert  = nullptr;

    ui_launcher_return();
    if (scr_old) lv_obj_delete_delayed(scr_old, 400);
}

static void back_cb(lv_event_t *) { do_close(); }

// ─── Création UI ──────────────────────────────────────────────────────────────
void radar_app_start() {
    orchestrator_set_app(APP_RADAR);

    audio_init();
    xTaskCreatePinnedToCore(beep_task, "radar_beep", 2048, nullptr, 5, &_beep_task_h, 1);

    _scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_btn_create(_scr);
    lv_obj_set_size(btn, 52, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_BG), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lb, lv_color_hex(C_TITLE), 0);
    lv_obj_center(lb);

    lv_obj_t *title = lv_label_create(_scr);
    lv_label_set_text(title, "Radars");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TITLE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    _lbl_gps = lv_label_create(_scr);
    lv_label_set_text(_lbl_gps, "GPS: attente...");
    lv_obj_set_style_text_font(_lbl_gps, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_gps, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_gps, LV_ALIGN_TOP_LEFT, 14, 86);

    _lbl_speed = lv_label_create(_scr);
    lv_label_set_text(_lbl_speed, "Vitesse: N/A");
    lv_obj_set_style_text_font(_lbl_speed, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_speed, lv_color_hex(C_NEUTRAL), 0);
    lv_obj_align(_lbl_speed, LV_ALIGN_TOP_RIGHT, -14, 86);

    _lbl_alert = lv_label_create(_scr);
    lv_label_set_text(_lbl_alert, "");
    lv_obj_set_style_text_font(_lbl_alert, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(_lbl_alert, lv_color_hex(C_WARN), 0);
    lv_obj_set_style_text_align(_lbl_alert, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(_lbl_alert, LV_ALIGN_TOP_MID, 0, 108);

    _lbl_status = lv_label_create(_scr);
    lv_label_set_text(_lbl_status, "");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_STATUS), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 132);

    _list = lv_obj_create(_scr);
    lv_obj_set_size(_list, 460, 280);
    lv_obj_align(_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(_list, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 6, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);

    refresh_list();

    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    _app_active    = true;
    _last_fetch    = 0;
    _alert_level   = 0;
    _alert_last_ms = 0;
    Serial.println("[APP/RADARS] Ouverte (mode autonome)");
}

// ─── Tick ─────────────────────────────────────────────────────────────────────
void radar_app_tick() {
    if (!_app_active || !_scr) return;

    double lat = 0.0, lon = 0.0;
    bool got_pos = false;

#ifdef GPS_AVAILABLE
    GpsData gps_hw;
    if (gps_get_data(&gps_hw) && gps_hw.valid) {
        lat = gps_hw.lat; lon = gps_hw.lon; got_pos = true;
    }
#endif
    if (!got_pos) got_pos = ble_mgr_get_gps(&lat, &lon);

    if (got_pos) {
        _has_gps = true;
        float delta = (_lat != 0.0 || _lon != 0.0)
                    ? haversine(_lat, _lon, lat, lon) : 9999.f;
        if (delta > MOVE_THRESH_M) {
            _lat = lat; _lon = lon;
            _last_fetch = 0;
            for (int i = 0; i < _nb_radars; i++)
                _radars[i].dist_m = haversine(_lat, _lon, _radars[i].lat, _radars[i].lon);
            sort_radars();
        }
        if (_lbl_gps) {
            char buf[52];
            snprintf(buf, sizeof(buf), "GPS: %.4f, %.4f", lat, lon);
            lv_label_set_text(_lbl_gps, buf);
        }
    } else {
        if (_lbl_gps) lv_label_set_text(_lbl_gps, "GPS: attente BLE...");
    }

    float spd;
    if (ble_mgr_get_speed(&spd)) _speed_kmh = spd;

    if (_has_gps) {
        uint32_t now = millis();
        if (!_fetch_running && (_last_fetch == 0 || (now - _last_fetch) >= FETCH_MS))
            start_fetch();
    }

    update_alert();
}

// ─── Stop ────────────────────────────────────────────────────────────────────
void radar_app_stop() { do_close(); }
