/**
 * App Bourse — Affiche 4 cours boursiers via Twelve Data
 * Tickers : CAC40, BTC/EUR, EUR/USD, XAU/USD
 */
#include "bourse_app.h"
#include "../../net/ble_mgr.h"
#include "../../system/orchestrator.h"
#include "../../config/nvs_config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>

// ─── Config ───────────────────────────────────────────────────────────────────
static const char *TICKERS[]    = { "FCHI", "BTC/EUR", "EUR/USD", "XAU/USD" };
static const char *LABELS[]     = { "CAC 40", "BTC/EUR", "EUR/USD", "OR/USD" };
static const int   TICKER_COUNT = 4;
static const char *API_BASE     = "https://api.twelvedata.com/price?symbol=%s&apikey=%s";
static const unsigned long REFRESH_MS = 60000UL;  // 1 min

// ─── État ─────────────────────────────────────────────────────────────────────
static lv_obj_t *_screen      = nullptr;
static lv_obj_t *_cards[4]    = {};
static lv_obj_t *_val_lbl[4]  = {};
static lv_obj_t *_name_lbl[4] = {};
static lv_obj_t *_status_lbl  = nullptr;
static lv_timer_t *_timer     = nullptr;
static unsigned long _last_fetch = 0;
static bool _open = false;
static char _api_key[64] = "";

// ─── Helpers UI ───────────────────────────────────────────────────────────────
static void set_status(const char *msg) {
    if (_status_lbl) lv_label_set_text(_status_lbl, msg);
}

static void update_card(int idx, float price, bool ok) {
    if (idx < 0 || idx >= TICKER_COUNT) return;
    char buf[32];
    if (ok) {
        if (price >= 1000.0f)
            snprintf(buf, sizeof(buf), "%.0f", price);
        else if (price >= 1.0f)
            snprintf(buf, sizeof(buf), "%.2f", price);
        else
            snprintf(buf, sizeof(buf), "%.4f", price);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    if (_val_lbl[idx]) lv_label_set_text(_val_lbl[idx], buf);
}

// ─── Fetch HTTP ───────────────────────────────────────────────────────────────
static void fetch_prices() {
    if (strlen(_api_key) == 0) {
        set_status("Clé API manquante");
        return;
    }
    set_status("Mise à jour...");

    HTTPClient http;
    for (int i = 0; i < TICKER_COUNT; i++) {
        char url[256];
        snprintf(url, sizeof(url), API_BASE, TICKERS[i], _api_key);
        http.begin(url);
        int code = http.GET();
        if (code == 200) {
            String body = http.getString();
            StaticJsonDocument<128> doc;
            if (!deserializeJson(doc, body) && doc.containsKey("price")) {
                float p = doc["price"].as<float>();
                update_card(i, p, true);
            } else {
                update_card(i, 0, false);
            }
        } else {
            update_card(i, 0, false);
        }
        http.end();
    }
    set_status("Mis à jour");
    _last_fetch = millis();
}

// ─── Timer callback ───────────────────────────────────────────────────────────
static void on_timer(lv_timer_t *) {
    if (!_open) return;
    if (millis() - _last_fetch >= REFRESH_MS) fetch_prices();
}

// ─── Bouton retour ────────────────────────────────────────────────────────────
static void on_back(lv_event_t *) {
    bourse_app_stop();
    orchestrator_set_app(APP_LAUNCHER);
}

// ─── Build UI ─────────────────────────────────────────────────────────────────
static void build_ui() {
    _screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_screen, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(_screen, LV_OPA_COVER, 0);

    // Titre
    lv_obj_t *title = lv_label_create(_screen);
    lv_label_set_text(title, "Bourse");
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // Bouton retour
    lv_obj_t *btn_back = lv_btn_create(_screen);
    lv_obj_set_size(btn_back, 60, 36);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);

    // 4 cartes 2×2
    int card_w = 180, card_h = 100;
    int pad_x = 20, pad_y = 70;
    for (int i = 0; i < TICKER_COUNT; i++) {
        int col = i % 2, row = i / 2;
        int x = pad_x + col * (card_w + 10);
        int y = pad_y + row * (card_h + 10);

        lv_obj_t *card = lv_obj_create(_screen);
        lv_obj_set_size(card, card_w, card_h);
        lv_obj_set_pos(card, x, y);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1e1e2e), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        _cards[i] = card;

        _name_lbl[i] = lv_label_create(card);
        lv_label_set_text(_name_lbl[i], LABELS[i]);
        lv_obj_set_style_text_color(_name_lbl[i], lv_color_hex(0xaaaaaa), 0);
        lv_obj_set_style_text_font(_name_lbl[i], &lv_font_montserrat_14, 0);
        lv_obj_align(_name_lbl[i], LV_ALIGN_TOP_LEFT, 10, 10);

        _val_lbl[i] = lv_label_create(card);
        lv_label_set_text(_val_lbl[i], "--");
        lv_obj_set_style_text_color(_val_lbl[i], lv_color_hex(0x00e5ff), 0);
        lv_obj_set_style_text_font(_val_lbl[i], &lv_font_montserrat_20, 0);
        lv_obj_align(_val_lbl[i], LV_ALIGN_BOTTOM_LEFT, 10, -10);
    }

    // Status bar
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
    for (int i = 0; i < TICKER_COUNT; i++) {
        _cards[i] = nullptr;
        _val_lbl[i] = nullptr;
        _name_lbl[i] = nullptr;
    }
    _status_lbl = nullptr;
}

// ─── API publique ─────────────────────────────────────────────────────────────
void bourse_app_start() {
    if (_open) return;

    // Charger la clé API depuis NVS
    String key = nvs_get_str("bourse_key", "");
    if (key.length() == 0) {
        Serial.println("[APP/BOURSE] Clé API manquante (NVS 'bourse_key')");
    }
    strlcpy(_api_key, key.c_str(), sizeof(_api_key));

    build_ui();
    lv_scr_load(_screen);
    _open = true;
    _last_fetch = 0;  // forcer fetch immédiat

    _timer = lv_timer_create(on_timer, 5000, NULL);
    fetch_prices();
    Serial.println("[APP/BOURSE] Ouverte");
}

void bourse_app_stop() {
    do_close();
}
