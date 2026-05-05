#include "meteo_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include "../../net/ble_mgr.h"
#include "../../config/secrets.h"
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>

// ─── Couleurs thème Météo ──────────────────────────────────────────────
#define C_BG    0x060E1A
#define C_TXT   0xFFCC44
#define C_MUTED 0xAA8822
#define C_CARD  0x0C1828

// ─── Prévision J+0..J+2 ────────────────────────────────────────────────
struct DayForecast {
    char date[12];      // "lun 5 mai"
    int  tmin, tmax;
    int  code;          // code météo (0-100 meteo-concept)
    int  rain10;        // proba pluie %
};
static DayForecast _days[3];
static int         _nb_days = 0;

// ─── État interne ────────────────────────────────────────────────────
static lv_obj_t *_scr         = nullptr;
static lv_obj_t *_lbl_gps     = nullptr;
static lv_obj_t *_lbl_status  = nullptr;
static lv_obj_t *_cards[3]    = {};

static double    _lat = 48.8566, _lon = 2.3522;  // Paris par défaut
static bool      _fetching   = false;
static uint32_t  _last_fetch = 0;
static bool      _app_active = false;

// ─── Noms jours ──────────────────────────────────────────────────────
static const char *JOURS[] = {"dim","lun","mar","mer","jeu","ven","sam"};
static const char *MOIS[]  = {"","jan","fev","mar","avr","mai","juin",
                               "juil","aou","sep","oct","nov","dec"};

// ─── Icône texte selon code météo ────────────────────────────────────
static const char *meteo_icon(int code) {
    if (code == 0)                    return LV_SYMBOL_WARNING; // inconnu
    if (code >= 1  && code <= 4)      return LV_SYMBOL_WIFI;    // ensoleillé (WiFi≈soleil)
    if (code >= 10 && code <= 16)     return LV_SYMBOL_WARNING; // nuageux
    if (code >= 20 && code <= 26)     return LV_SYMBOL_AUDIO;   // pluie légère
    if (code >= 40 && code <= 48)     return LV_SYMBOL_DOWN;    // pluie forte
    if (code >= 60 && code <= 78)     return LV_SYMBOL_DOWN;    // neige
    if (code >= 100 && code <= 102)   return LV_SYMBOL_UP;      // orage
    return LV_SYMBOL_WARNING;
}

static const char *meteo_label(int code) {
    if (code == 0)                    return "N/A";
    if (code >= 1  && code <= 4)      return "Ensoleille";
    if (code >= 10 && code <= 16)     return "Nuageux";
    if (code >= 20 && code <= 26)     return "Pluie";
    if (code >= 40 && code <= 48)     return "Forte pluie";
    if (code >= 60 && code <= 78)     return "Neige";
    if (code >= 100 && code <= 102)   return "Orage";
    return "Variable";
}

// ─── Mise à jour affichage cartes ─────────────────────────────────────
static void refresh_cards() {
    for (int i = 0; i < 3; i++) {
        if (!_cards[i]) continue;
        lv_obj_t *card = _cards[i];
        // On recrée les labels enfants (card est vide à la création)
        // Nettoyage
        lv_obj_clean(card);

        if (i >= _nb_days) {
            lv_obj_t *lbl = lv_label_create(card);
            lv_label_set_text(lbl, "---");
            lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
            lv_obj_center(lbl);
            continue;
        }
        const DayForecast &d = _days[i];

        lv_obj_t *lbl_date = lv_label_create(card);
        lv_label_set_text(lbl_date, d.date);
        lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_date, lv_color_hex(C_MUTED), 0);
        lv_obj_align(lbl_date, LV_ALIGN_TOP_MID, 0, 6);

        lv_obj_t *ico = lv_label_create(card);
        lv_label_set_text(ico, meteo_icon(d.code));
        lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(ico, lv_color_hex(C_TXT), 0);
        lv_obj_align(ico, LV_ALIGN_TOP_MID, 0, 28);

        char cond[20]; snprintf(cond, sizeof(cond), "%s", meteo_label(d.code));
        lv_obj_t *lbl_cond = lv_label_create(card);
        lv_label_set_text(lbl_cond, cond);
        lv_obj_set_style_text_font(lbl_cond, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_cond, lv_color_hex(C_TXT), 0);
        lv_obj_set_style_text_align(lbl_cond, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl_cond, LV_ALIGN_TOP_MID, 0, 56);

        char temp[20]; snprintf(temp, sizeof(temp), "%d / %d C", d.tmin, d.tmax);
        lv_obj_t *lbl_t = lv_label_create(card);
        lv_label_set_text(lbl_t, temp);
        lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(lbl_t, lv_color_hex(C_TXT), 0);
        lv_obj_align(lbl_t, LV_ALIGN_BOTTOM_MID, 0, -22);

        char rain[16]; snprintf(rain, sizeof(rain), "Pluie: %d%%", d.rain10);
        lv_obj_t *lbl_r = lv_label_create(card);
        lv_label_set_text(lbl_r, rain);
        lv_obj_set_style_text_font(lbl_r, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_r, lv_color_hex(C_MUTED), 0);
        lv_obj_align(lbl_r, LV_ALIGN_BOTTOM_MID, 0, -6);
    }
}

// ─── Fetch météo HTTP (bloquant court — WiFi doit être up) ─────────────
static void do_fetch() {
    if (!WiFi.isConnected()) {
        if (_lbl_status) lv_label_set_text(_lbl_status, "WiFi non connecte");
        return;
    }
    if (_lbl_status) lv_label_set_text(_lbl_status, "Chargement...");
    lv_task_handler();

    // Trouver le code INSEE depuis lat/lon : on utilise l'endpoint
    // https://api.meteo-concept.com/api/location/near?latlng=lat,lon&token=KEY
    // puis on fetch les prévisions J+3 avec le code retourné
    String url_loc = "https://api.meteo-concept.com/api/location/near?latlng=";
    url_loc += String(_lat, 5) + "," + String(_lon, 5);
    url_loc += "&token=" + String(API_KEY_METEO);

    HTTPClient http;
    http.setTimeout(6000);
    http.begin(url_loc);
    int rc = http.GET();
    if (rc != 200) {
        if (_lbl_status) {
            char buf[48]; snprintf(buf, sizeof(buf), "Erreur loc %d", rc);
            lv_label_set_text(_lbl_status, buf);
        }
        http.end();
        return;
    }

    DynamicJsonDocument doc_loc(2048);
    deserializeJson(doc_loc, http.getString());
    http.end();

    int insee = doc_loc["city"]["insee"] | 0;
    if (insee == 0) {
        if (_lbl_status) lv_label_set_text(_lbl_status, "Ville introuvable");
        return;
    }
    const char *city_name = doc_loc["city"]["nom"] | "?";
    if (_lbl_gps) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s (%.3f,%.3f)", city_name, _lat, _lon);
        lv_label_set_text(_lbl_gps, buf);
    }

    // Prévisions J+3
    String url_fc = "https://api.meteo-concept.com/api/forecast/daily?";
    url_fc += "insee=" + String(insee);
    url_fc += "&token=" + String(API_KEY_METEO);

    HTTPClient http2;
    http2.setTimeout(8000);
    http2.begin(url_fc);
    rc = http2.GET();
    if (rc != 200) {
        if (_lbl_status) {
            char buf[48]; snprintf(buf, sizeof(buf), "Erreur previsions %d", rc);
            lv_label_set_text(_lbl_status, buf);
        }
        http2.end();
        return;
    }

    DynamicJsonDocument doc_fc(4096);
    deserializeJson(doc_fc, http2.getString());
    http2.end();

    JsonArray arr = doc_fc["forecast"].as<JsonArray>();
    _nb_days = 0;
    for (JsonObject day : arr) {
        if (_nb_days >= 3) break;
        DayForecast &d = _days[_nb_days];
        const char *dt = day["datetime"] | "0000-00-00";
        // Parse YYYY-MM-DD → "lun 5 mai"
        int yr = 0, mo = 0, da = 0;
        sscanf(dt, "%d-%d-%d", &yr, &mo, &da);
        // Calcul jour semaine (Zeller)
        if (mo < 3) { mo += 12; yr--; }
        int dow = (da + (13*(mo+1))/5 + yr + yr/4 - yr/100 + yr/400) % 7;
        // Zeller: 0=sam,1=dim,2=lun...
        static const int zmap[] = {6,0,1,2,3,4,5};
        int wd = zmap[((dow % 7) + 7) % 7];
        snprintf(d.date, sizeof(d.date), "%s %d %s", JOURS[wd], da,
                 (mo > 12 ? mo-12 : mo) >= 1 && (mo > 12 ? mo-12 : mo) <= 12
                 ? MOIS[mo > 12 ? mo-12 : mo] : "?");
        d.tmin    = day["tmin"]    | 0;
        d.tmax    = day["tmax"]    | 0;
        d.code    = day["weather"] | 0;
        d.rain10  = day["probarain"] | 0;
        _nb_days++;
    }

    if (_lbl_status) lv_label_set_text(_lbl_status, "");
    refresh_cards();
    Serial.printf("[METEO] %d jours recuperes (insee=%d)\n", _nb_days, insee);
}

// ─── Bouton retour ────────────────────────────────────────────────────
static void back_cb(lv_event_t *) {
    meteo_app_stop();
    ui_launcher_return();
}

// ─── Création UI ─────────────────────────────────────────────────────
void meteo_app_start() {
    orchestrator_set_app(APP_METEO);
    _app_active  = true;
    _last_fetch  = 0;  // forcer fetch immédiat

    _scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(_scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr, LV_OBJ_FLAG_SCROLLABLE);

    // Bouton retour
    lv_obj_t *btn = lv_btn_create(_scr);
    lv_obj_set_size(btn, 52, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn, lv_color_hex(C_BG), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lb, lv_color_hex(C_TXT), 0);
    lv_obj_center(lb);

    // Titre
    lv_obj_t *title = lv_label_create(_scr);
    lv_label_set_text(title, "Meteo");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    // Label GPS / ville
    _lbl_gps = lv_label_create(_scr);
    lv_label_set_text(_lbl_gps, "En attente position BLE...");
    lv_obj_set_style_text_font(_lbl_gps, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_gps, lv_color_hex(C_MUTED), 0);
    lv_obj_align(_lbl_gps, LV_ALIGN_TOP_MID, 0, 82);

    // Label statut (erreur / chargement)
    _lbl_status = lv_label_create(_scr);
    lv_label_set_text(_lbl_status, "");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(0xFF6644), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 104);

    // 3 cartes prévisions côte à côte
    // Écran 480px → 3 cartes 140px + 2 gaps 12px = 444px centré
    static const int CARD_W = 140, CARD_H = 180;
    static const int GAP    = 12;
    int total_w = 3 * CARD_W + 2 * GAP;   // 444
    int start_x = (480 - total_w) / 2;    // 18
    int card_y  = 130;

    for (int i = 0; i < 3; i++) {
        _cards[i] = lv_obj_create(_scr);
        lv_obj_set_size(_cards[i], CARD_W, CARD_H);
        lv_obj_set_pos(_cards[i], start_x + i * (CARD_W + GAP), card_y);
        lv_obj_set_style_bg_color(_cards[i], lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(_cards[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(_cards[i], lv_color_hex(C_TXT), 0);
        lv_obj_set_style_border_width(_cards[i], 1, 0);
        lv_obj_set_style_border_opa(_cards[i], LV_OPA_40, 0);
        lv_obj_set_style_radius(_cards[i], 14, 0);
        lv_obj_clear_flag(_cards[i], LV_OBJ_FLAG_SCROLLABLE);

        // Placeholder
        lv_obj_t *ph = lv_label_create(_cards[i]);
        lv_label_set_text(ph, "...");
        lv_obj_set_style_text_color(ph, lv_color_hex(C_MUTED), 0);
        lv_obj_center(ph);
    }

    // Légende BLE
    lv_obj_t *hint = lv_label_create(_scr);
    lv_label_set_text(hint, "Position via BLE telephone | WiFi requis");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x334455), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    Serial.println("[APP/METEO] Ouverte");
}

// ─── Tick (appelé par orchestrator_tick) ─────────────────────────────
void meteo_app_tick() {
    if (!_app_active || !_scr) return;

    // Mise à jour position GPS depuis BLE
    double lat, lon;
    if (ble_mgr_get_gps(&lat, &lon)) {
        if (lat != _lat || lon != _lon) {
            _lat = lat;
            _lon = lon;
            _last_fetch = 0;  // force re-fetch si position change
        }
    }

    // Fetch toutes les 10 min ou au premier appel
    uint32_t now = millis();
    if (now - _last_fetch > 600000UL || _last_fetch == 0) {
        _last_fetch = now;
        do_fetch();
    }
}

// ─── Stop ─────────────────────────────────────────────────────────────
void meteo_app_stop() {
    _app_active = false;
    if (_scr) { lv_obj_del(_scr); _scr = nullptr; }
    for (int i = 0; i < 3; i++) _cards[i] = nullptr;
    _lbl_gps = nullptr; _lbl_status = nullptr;
    orchestrator_set_app(APP_LAUNCHER);
}
