/**
 * App Bourse — Affiche 4 cours boursiers via Twelve Data
 * Tickers : CAC40, BTC/EUR, EUR/USD, XAU/USD
 * Refresh : 90 s si marché ouvert (9h-18h lun-ven), 10 min sinon
 * Résolution : 480×480 px (écran carré Compagnon)
 *
 * Clé API Twelve Data : fournie par la PWA via BLE
 *   commande : cfg:twelve_data_key:<VOTRE_CLE>
 *   La clé est persistée en NVS (namespace "cfg", clé "twelve_key").
 */
#include "bourse_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <Preferences.h>
#include <time.h>

// ─── Clé API Twelve Data (chargée depuis NVS, écrite par la PWA via BLE) ──────
char g_twelve_data_key[64] = {};

static void load_twelve_key() {
    Preferences p;
    p.begin("cfg", true);
    strlcpy(g_twelve_data_key, p.getString("twelve_key", "").c_str(), sizeof(g_twelve_data_key));
    p.end();
}

// Appeler depuis le gestionnaire BLE quand la commande cfg:twelve_data_key:<val> arrive
void bourse_set_twelve_key(const char *key) {
    strlcpy(g_twelve_data_key, key, sizeof(g_twelve_data_key));
    Preferences p;
    p.begin("cfg", false);
    p.putString("twelve_key", key);
    p.end();
    Serial.printf("[BOURSE] Clé Twelve Data mise à jour (%d chars)\n", (int)strlen(key));
}

// ─── Thème ────────────────────────────────────────────────────────────────────
#define C_BG      0x061A06
#define C_CARD    0x0B2A0B
#define C_TITLE   0x66EE88
#define C_MUTED   0x336633
#define C_UP      0x44DD66
#define C_DOWN    0xEE4444
#define C_NEUTRAL 0xCCCCCC
#define C_STATUS  0x558855

// ─── Tickers ──────────────────────────────────────────────────────────────────
struct Ticker {
    const char *symbol;
    const char *label;
    float price;
    float change;
    float pct;
    bool valid;
};

static Ticker _tickers[] = {
    { "CAC40",   "CAC 40",   0, 0, 0, false },
    { "BTC/EUR", "Bitcoin",  0, 0, 0, false },
    { "EUR/USD", "EUR/USD",  0, 0, 0, false },
    { "XAU/USD", "Or (once)",0, 0, 0, false },
};
static constexpr int NB_TICKERS = 4;

// ─── État UI ──────────────────────────────────────────────────────────────────
static lv_obj_t *_scr        = nullptr;
static lv_obj_t *_lbl_status = nullptr;
static lv_obj_t *_rows[NB_TICKERS] = {};

static bool     _app_active   = false;
static uint32_t _last_fetch   = 0;
static volatile bool _fetch_running = false;
static char     _err_buf[64]  = {};

// ─── Marché ouvert ? (9h-18h lun-ven, heure locale) ──────────────────────────
static bool market_open() {
    time_t now = time(nullptr);
    struct tm *t = localtime(&now);
    if (!t) return false;
    if (t->tm_wday == 0 || t->tm_wday == 6) return false;
    return t->tm_hour >= 9 && t->tm_hour < 18;
}

// ─── Formatage prix ───────────────────────────────────────────────────────────
static void fmt_price(char *buf, size_t sz, int idx, float price) {
    if (idx == 2)          snprintf(buf, sz, "%.4f", price);
    else if (price >= 1000.f) snprintf(buf, sz, "%.0f", price);
    else                   snprintf(buf, sz, "%.2f", price);
}

// ─── Mise à jour d'une ligne ──────────────────────────────────────────────────
static void refresh_row(int i) {
    lv_obj_t *row = _rows[i];
    if (!row) return;
    lv_obj_clean(row);

    const Ticker &tk = _tickers[i];

    lv_obj_t *lbl = lv_label_create(row);
    lv_label_set_text(lbl, tk.label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(C_NEUTRAL), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 10, 0);

    if (!tk.valid) {
        lv_obj_t *na = lv_label_create(row);
        lv_label_set_text(na, "---");
        lv_obj_set_style_text_font(na, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(na, lv_color_hex(C_MUTED), 0);
        lv_obj_align(na, LV_ALIGN_RIGHT_MID, -10, 0);
        return;
    }

    bool up = (tk.change >= 0.0f);
    uint32_t col = up ? C_UP : C_DOWN;

    char pbuf[24];
    fmt_price(pbuf, sizeof(pbuf), i, tk.price);
    lv_obj_t *lbl_p = lv_label_create(row);
    lv_label_set_text(lbl_p, pbuf);
    lv_obj_set_style_text_font(lbl_p, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_p, lv_color_hex(col), 0);
    lv_obj_align(lbl_p, LV_ALIGN_RIGHT_MID, -10, -9);

    char vbuf[24];
    snprintf(vbuf, sizeof(vbuf), "%s%.2f%%", up ? "+" : "", tk.pct);
    lv_obj_t *lbl_v = lv_label_create(row);
    lv_label_set_text(lbl_v, vbuf);
    lv_obj_set_style_text_font(lbl_v, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_v, lv_color_hex(up ? 0x2A8A44 : 0x8A2A2A), 0);
    lv_obj_align(lbl_v, LV_ALIGN_RIGHT_MID, -10, 10);

    lv_obj_set_style_border_color(row, lv_color_hex(up ? 0x1A4A1A : 0x4A1A1A), 0);
}

// ─── Callback LVGL (depuis la tâche HTTP) ────────────────────────────────────
static void on_fetch_done(void *) {
    _fetch_running = false;
    if (!_scr) return;
    for (int i = 0; i < NB_TICKERS; i++) refresh_row(i);
    if (_lbl_status) {
        time_t now = time(nullptr);
        struct tm *t = localtime(&now);
        char buf[48];
        snprintf(buf, sizeof(buf), "Mis à jour %02d:%02d%s",
                 t ? t->tm_hour : 0, t ? t->tm_min : 0,
                 market_open() ? "" : " (marché fermé)");
        lv_label_set_text(_lbl_status, buf);
    }
}

// ─── Tâche FreeRTOS — fetch Twelve Data ──────────────────────────────────────
static void fetch_task(void *) {
    if (!WiFi.isConnected()) {
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "WiFi non connecté");
        }, nullptr);
        vTaskDelete(NULL); return;
    }

    if (g_twelve_data_key[0] == '\0') {
        lv_async_call([](void *) {
            _fetch_running = false;
            if (_lbl_status) lv_label_set_text(_lbl_status, "Clé API manquante (PWA → Config)");
        }, nullptr);
        vTaskDelete(NULL); return;
    }

    String symbols;
    for (int i = 0; i < NB_TICKERS; i++) {
        if (i > 0) symbols += ",";
        symbols += _tickers[i].symbol;
    }

    String url = "https://api.twelvedata.com/quote?symbol=";
    url += symbols;
    url += "&apikey=";
    url += g_twelve_data_key;
    url += "&dp=2";

    HTTPClient http;
    http.setTimeout(8000);
    http.begin(url);
    int rc = http.GET();

    if (rc != 200) {
        snprintf(_err_buf, sizeof(_err_buf), "API erreur %d", rc);
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
        tk.change = atof(q["change"]         | "0");
        tk.pct    = atof(q["percent_change"] | "0");
        tk.valid  = (tk.price > 0.0f);
        if (tk.change == 0.0f && tk.valid) {
            float prev = atof(q["previous_close"] | "0");
            tk.change  = tk.price - prev;
            if (prev > 0.0f) tk.pct = (tk.change / prev) * 100.0f;
        }
    }

    Serial.println("[APP/BOURSE] Tickers mis à jour");
    lv_async_call(on_fetch_done, nullptr);
    vTaskDelete(NULL);
}

// ─── Bouton retour ────────────────────────────────────────────────────────────
static void back_cb(lv_event_t *) { bourse_app_stop(); ui_launcher_return(); }

// ─── Création UI ──────────────────────────────────────────────────────────────
void bourse_app_start() {
    load_twelve_key();
    orchestrator_set_app(APP_BOURSE);

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
    lv_obj_set_style_text_color(lb, lv_color_hex(C_TITLE), 0);
    lv_obj_center(lb);

    // Titre
    lv_obj_t *title = lv_label_create(_scr);
    lv_label_set_text(title, "Bourse");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(C_TITLE), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

    // Label statut
    _lbl_status = lv_label_create(_scr);
    lv_label_set_text(_lbl_status, g_twelve_data_key[0] ? "Chargement..." : "Config clé API dans la PWA");
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(_lbl_status, lv_color_hex(C_STATUS), 0);
    lv_obj_align(_lbl_status, LV_ALIGN_TOP_MID, 0, 86);

    // 4 lignes ticker
    static const int ROW_H = 78;
    static const int GAP   = 8;
    static const int ROW_W = 450;
    int total_h = NB_TICKERS * ROW_H + (NB_TICKERS - 1) * GAP;
    int start_y = (480 - total_h) / 2 + 28;

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
        refresh_row(i);
    }

    // Légende
    lv_obj_t *hint = lv_label_create(_scr);
    lv_label_set_text(hint, "Twelve Data | 90 s / 10 min");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x1A3A1A), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    lv_scr_load_anim(_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    _app_active = true;
    _last_fetch = 0;
    Serial.println("[APP/BOURSE] Ouverte");
}

// ─── Tick ─────────────────────────────────────────────────────────────────────
void bourse_app_tick() {
    if (!_app_active || !_scr) return;
    if (g_twelve_data_key[0] == '\0') return; // pas de clé → ne pas appeler l'API
    uint32_t now = millis();
    uint32_t interval = market_open() ? 90000UL : 600000UL;
    if (!_fetch_running && (_last_fetch == 0 || (now - _last_fetch) >= interval)) {
        _last_fetch = now;
        _fetch_running = true;
        if (_lbl_status) lv_label_set_text(_lbl_status, "Chargement...");
        xTaskCreatePinnedToCore(fetch_task, "bourse_http", 8192, nullptr, 1, nullptr, 0);
    }
}

// ─── Stop ─────────────────────────────────────────────────────────────────────
void bourse_app_stop() {
    _app_active = false;
    if (_scr) { lv_obj_del(_scr); _scr = nullptr; }
    for (int i = 0; i < NB_TICKERS; i++) _rows[i] = nullptr;
    _lbl_status = nullptr;
    orchestrator_set_app(APP_LAUNCHER);
    Serial.println("[APP/BOURSE] Fermée");
}
