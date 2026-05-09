/**
 * Afficher 4 cours boursiers en temps réel via Twelve Data.
 * Tickers : CAC40, BTC/EUR, EUR/USD, XAU/USD
 * Refresh  : 90 s si marché ouvert (9h–18h lun–ven), 10 min sinon
 */
#include "bourse_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include "../../config/secrets.h"
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <time.h>

// ─── Thème ────────────────────────────────────────────────────────────────────
#define C_BG      0x061A06
#define C_CARD    0x0B1F0B
#define C_TITLE   0x66EE88
#define C_MUTED   0x336633
#define C_UP      0x44DD66
#define C_DOWN    0xEE4444
#define C_NEUTRAL 0xCCCCCC

// ─── Ticker ───────────────────────────────────────────────────────────────────
struct Ticker {
    const char *symbol;   // symbole Twelve Data
    const char *label;    // nom court affiché
    float  price;
    float  change;        // variation absolue
    float  pct;           // variation %
    bool   valid;
};

static Ticker _tickers[] = {
    { "CAC40",   "CAC 40",    0, 0, 0, false },
    { "BTC/EUR", "Bitcoin",   0, 0, 0, false },
    { "EUR/USD", "EUR/USD",   0, 0, 0, false },
    { "XAU/USD", "Or (once)", 0, 0, 0, false },
};
static constexpr int NB_TICKERS = 4;

// ─── État UI ──────────────────────────────────────────────────────────────────
static lv_obj_t *_scr         = nullptr;
static lv_obj_t *_lbl_status  = nullptr;
static lv_obj_t *_rows[NB_TICKERS] = {};

static bool      _app_active  = false;
static uint32_t  _last_fetch  = 0;
static volatile bool _fetch_running = false;
static char      _err_buf[48] = {};

// ─── Marché ouvert ? (9h–18h lun–ven, heure locale) ─────────────────────────
static bool market_open() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    if (t->tm_wday == 0 || t->tm_wday == 6) return false;   // week-end
    return t->tm_hour >= 9 && t->tm_hour < 18;
}

// ─── Formatage prix selon symbole ────────────────────────────────────────────
static void fmt_price(char *buf, size_t sz, int idx, float price) {
    if (idx == 2) {                        // EUR/USD : 4 décimales
        snprintf(buf, sz, "%.4f", price);
    } else if (price >= 1000.0f) {         // entier avec séparateur
        snprintf(buf, sz, "%.0f", price);
    } else {
        snprintf(buf, sz, "%.2f", price);
    }
}

// ─── Mise à jour visuelle d'une ligne ────────────────────────────────────────
static void refresh_row(int i) {
    lv_obj_t *row = _rows[i];
    if (!row) return;
    lv_obj_clean(row);

    const Ticker &tk = _tickers[i];

    // Étiquette gauche
    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, tk.label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(C_NEUTRAL), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

    if (!tk.valid) {
        lv_obj_t *na = lv_label_create(row);
        lv_label_set_text(na, "---");
        lv_obj_set_style_text_font(na, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(na, lv_color_hex(C_MUTED), 0);
        lv_obj_align(na, LV_ALIGN_RIGHT_MID, -8, 0);
        return;
    }

    bool up = tk.change >= 0.0f;
    uint32_t col_price = up ? C_UP : C_DOWN;

    // Prix (droite)
    char pbuf[24];
    fmt_price(pbuf, sizeof(pbuf), i, tk.price);
    lv_obj_t *lbl_price = lv_label_create(row);
    lv_label_set_text(lbl_price, pbuf);
    lv_obj_set_style_text_font(lbl_price, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_price, lv_color_hex(col_price), 0);
    lv_obj_align(lbl_price, LV_ALIGN_RIGHT_MID, -8, -9);

    // Variation % (sous le prix)
    char vbuf[24];
    snprintf(vbuf, sizeof(vbuf), "%s%.2f%%", up ? "+" : "", tk.pct);
    lv_obj_t *lbl_var = lv_label_create(row);
    lv_label_set_text(lbl_var, vbuf);
    lv_obj_set_style_text_font(lbl_var, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_var, lv_color_hex(up ? 0x2A8A44 : 0x8A2A2A), 0);
    lv_obj_align(lbl_var, LV_ALIGN_RIGHT_MID, -8, 10);

    // Bordure couleur
    lv_obj_set_style_border_color(row, lv_color_hex(up ? 0x1A3A1A : 0x3A1A1A), 0);
}

// ─── Callback LVGL (posté depuis la tâche HTTP) ───────────────────────────────
static void on_fetch_done(void *) {
    _fetch_running = false;
    if (!_scr) return;
    for (int i = 0; i < NB_TICKERS; i++) refresh_row(i);
    if (_lbl_status) {
        struct tm *t = localtime(nullptr);
        char buf[32];
        snprintf(buf, sizeof(buf), "%02d:%02d%s",
                 t ? t->tm_hour : 0, t ? t->tm_min : 0,
                 market_open() ? "" : " (ferm\xc3\xa9)");
        lv_label_set_text(_lbl_status, buf);
    }
}

// ─── Tâche FreeRTOS — fetch Twelve Data ──────────────────────────────────────
static void fetch_task(void *) {
    if (!WiFi.isConnected()) {
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "WiFi non connecte");
        }, nullptr);
        vTaskDelete(NULL); return;
    }

    // Construit la liste de symboles : "CAC40,BTC/EUR,EUR/USD,XAU/USD"
    String symbols_str;
    for (int i = 0; i < NB_TICKERS; i++) {
        if (i > 0) symbols_str += ",";
        symbols_str += _tickers[i].symbol;
    }

    String url = "https://api.twelvedata.com/quote?symbol=";
    url += symbols_str;
    url += "&apikey=";
    url += TWELVE_DATA_KEY;
    url += "&dp=2";

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    int rc = http.GET();

    if (rc != 200) {
        char buf[48]; snprintf(buf, sizeof(buf), "Erreur API %d", rc);
        strncpy(_err_buf, buf, sizeof(_err_buf));
        http.end();
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, _err_buf);
        }, nullptr);
        vTaskDelete(NULL); return;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError derr = deserializeJson(doc, http.getString());
    http.end();

    if (derr) {
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "JSON invalide");
        }, nullptr);
        vTaskDelete(NULL); return;
    }

    for (int i = 0; i < NB_TICKERS; i++) {
        Ticker &tk = _tickers[i];
        JsonObject q = doc[tk.symbol];
        if (q.isNull() || q["status"] == "error") { tk.valid = false; continue; }
        tk.price  = atof(q["close"]          | "0");
        tk.change = atof(q["change"]          | "0");
        tk.pct    = atof(q["percent_change"]  | "0");
        tk.valid  = (tk.price > 0.0f);
        if (tk.change == 0.0f && tk.valid) {
            float prev = atof(q["previous_close"] | "0");
            tk.change = tk.price - prev;
            if (prev > 0.0f) tk.pct = (tk.change / prev) * 100.0f;
        }
    }

    Serial.println("[APP/BOURSE] Tickers mis a jour");
    lv_async_call(on_fetch_done, nullptr);
    vTaskDelete(NULL);
}

// ─── Bouton retour ────────────────────────────────────────────────────────────
static void back_cb(lv_event_t *) { bourse_app_stop(); ui_launcher_return(); }

// ─── Création UI ──────────────────────────────────────────────────────────────
void bourse_app_start() {
    orchestrator_set_app(APP_BOURSE);
    _scr_bourse = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr_bourse, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_scr_bourse, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr_bourse, LV_OBJ_FLAG_SCROLLABLE);

    // bouton retour — fond noir transparent
    lv_obj_t *btn = lv_btn_create(_scr_bourse);
    lv_obj_set_size(btn, 52, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lb, lv_color_hex(C_TITLE), 0);
    lv_obj_center(lb);

    // Titre
    lv_obj_t *title = lv_label_create(_scr);
    lv_label_set_text(title, "Bourse");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TITLE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 48);

    // carte principale — or sombre semi-transparent
    lv_obj_t *card = lv_obj_create(_scr_bourse);
    lv_obj_set_size(card, 380, 140);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x4a3000), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_50, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *body = lv_label_create(card);
    lv_label_set_text(body, "Marches & Portefeuille\n\n(en cours de developpement)");
    lv_obj_set_style_text_font(body, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(body, 340);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 0);

    // 4 lignes tickers (hauteur 76 px, espacées de 8 px, centrées verticalement)
    static const int ROW_H = 76;
    static const int GAP   = 8;
    static const int ROW_W = 440;
    int total_h = NB_TICKERS * ROW_H + (NB_TICKERS - 1) * GAP;
    int start_y = (480 - total_h) / 2 + 30;  // légèrement décalé vers le bas

    for (int i = 0; i < NB_TICKERS; i++) {
        lv_obj_t *row = lv_obj_create(_scr);
        lv_obj_set_size(row, ROW_W, ROW_H);
        lv_obj_set_pos(row, (480 - ROW_W) / 2, start_y + i * (ROW_H + GAP));
        lv_obj_set_style_bg_color(row, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_MUTED), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        _rows[i] = row;
        refresh_row(i);  // affiche "---" initialement
    }

    // Légende bas de page
    lv_obj_t *hint = lv_label_create(_scr);
    lv_label_set_text(hint, "Twelve Data  |  Refresh 90 s");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x223322), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    Serial.println("[APP/BOURSE] Ouverte");
}

// ─── Tick (appelé par orchestrator_tick si app active) ────────────────────────
void bourse_app_tick() {
    if (!_app_active || !_scr) return;

    uint32_t now      = millis();
    uint32_t interval = market_open() ? 90000UL : 600000UL;

    if (!_fetch_running && (_last_fetch == 0 || (now - _last_fetch) >= interval)) {
        _last_fetch    = now;
        _fetch_running = true;
        if (_lbl_status) lv_label_set_text(_lbl_status, "Chargement...");
        xTaskCreatePinnedToCore(fetch_task, "bourse_http", 8192, nullptr, 1, nullptr, 0);
    }
}

// ─── Stop ──────────────────────────────────────────────────────────────────────
void bourse_app_stop() {
    _app_active = false;
    if (_scr) { lv_obj_del(_scr); _scr = nullptr; }
    for (int i = 0; i < NB_TICKERS; i++) _rows[i] = nullptr;
    _lbl_status = nullptr;
    orchestrator_set_app(APP_LAUNCHER);
}
