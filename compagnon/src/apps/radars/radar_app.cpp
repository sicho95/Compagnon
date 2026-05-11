/**
 * App Radars — Affiche les radars proches via API Lufop
 * Position GPS : depuis BLE téléphone (float32 LE), fallback Paris
 * Refresh       : 5 min ou dès que déplacement > 50 m
 * Tri           : distance croissante
 * Couleurs      : rouge < 500 m, orange < 2 km, vert sinon
 * Résolution    : 480×480 px (écran carré Compagnon)
 */
#include "radar_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include "../../net/ble_mgr.h"
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

#define MAX_RADARS 20
#define FETCH_MS   300000UL

// ─── Radar ────────────────────────────────────────────────────────────────────
struct Radar {
    float lat, lon;
    int   speed;
    char  type[16];
    float dist_m;
};

static Radar _radars[MAX_RADARS];
static int   _nb_radars = 0;

// ─── État UI ─────────────────────────────────────────────────────────────────
static lv_obj_t *_scr        = nullptr;
static lv_obj_t *_lbl_gps    = nullptr;
static lv_obj_t *_lbl_status = nullptr;
static lv_obj_t *_list       = nullptr;

static bool     _app_active  = false;
static uint32_t _last_fetch  = 0;
static double   _lat = 0.0, _lon = 0.0;
static bool     _has_gps     = false;
static volatile bool _fetch_running = false;

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
    if (_lbl_status) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d radar(s) trouvé(s)", _nb_radars);
        lv_label_set_text(_lbl_status, buf);
    }
}

// ─── Tâche FreeRTOS — fetch Lufop ────────────────────────────────────────────
static void fetch_task(void *) {
    if (!WiFi.isConnected()) {
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "WiFi non connecté");
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
        char *msg = (char *)malloc(48);
        if (msg) snprintf(msg, 48, "Lufop erreur %d", rc);
        http.end();
        lv_async_call([](void *p) {
            _fetch_running = false;
            if (_lbl_status && p) lv_label_set_text(_lbl_status, (char *)p);
            free(p);
        }, msg);
        vTaskDelete(NULL); return;
    }

    DynamicJsonDocument doc(16384);
    if (doc.capacity() == 0) {
        http.end();
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "Mémoire insuffisante");
        }, nullptr);
        vTaskDelete(NULL); return;
    }

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

// ─── Close ────────────────────────────────────────────────────────────────────
static void do_close() {
    if (!_app_active) return;
    _app_active    = false;
    _fetch_running = false;
    orchestrator_set_app(APP_LAUNCHER);
    Serial.println("[APP/RADARS] Fermée");

    lv_obj_t *scr_old = _scr;
    _scr        = nullptr;
    _list       = nullptr;
    _lbl_gps    = nullptr;
    _lbl_status = nullptr;

    // Retour vers le launcher puis supprime l'écran après l'animation
    ui_launcher_return();
    if (scr_old) lv_obj_delete_delayed(scr_old, 400);
}

static void back_cb(lv_event_t *) { do_close(); }

// ─── Création UI ──────────────────────────────────────────────────────────────
void radar_app_start() {
    orchestrator_set_app(APP_RADAR);

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
    lv_label_set_text(_lbl_gps, "GPS: attente BLE...");
    lv_obj_set_style_text_font(_lbl_gps, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_gps, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_gps, LV_ALIGN_TOP_MID, 0, 84);

    _lbl_status = lv_label_create(_scr);
    lv_label_set_text(_lbl_status, "");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_STATUS), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 104);

    _list = lv_obj_create(_scr);
    lv_obj_set_size(_list, 460, 300);
    lv_obj_align(_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(_list, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 6, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);

    refresh_list();

    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    _app_active = true;
    _last_fetch = 0;
    Serial.println("[APP/RADARS] Ouverte");
}

// ─── Tick ─────────────────────────────────────────────────────────────────────
void radar_app_tick() {
    if (!_app_active || !_scr) return;

    double lat, lon;
    if (ble_mgr_get_gps(&lat, &lon)) {
        _has_gps = true;
        float delta = (_lat != 0.0 || _lon != 0.0)
                    ? haversine(_lat, _lon, lat, lon) : 9999.f;
        if (delta > 50.f) {
            _lat = lat; _lon = lon;
            _last_fetch = 0;
        }
        if (_lbl_gps) {
            char buf[48];
            snprintf(buf, sizeof(buf), "GPS: %.4f, %.4f", lat, lon);
            lv_label_set_text(_lbl_gps, buf);
        }
    }

    if (!_has_gps) return;

    uint32_t now = millis();
    if (!_fetch_running && (_last_fetch == 0 || (now - _last_fetch) >= FETCH_MS)) {
        _last_fetch    = now;
        _fetch_running = true;
        if (_lbl_status) lv_label_set_text(_lbl_status, "Chargement...");
        xTaskCreatePinnedToCore(fetch_task, "radar_http", 8192, nullptr, 1, nullptr, 0);
    }
}

// ─── Stop ─────────────────────────────────────────────────────────────────────
void radar_app_stop() {
    do_close();
}
