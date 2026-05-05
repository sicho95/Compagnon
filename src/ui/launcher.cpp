#include "launcher.h"
#include "../config/pin_config.h"
#include "../hal/pmu.h"
#include "../system/orchestrator.h"
#include "../apps/nestor/nestor_app.h"
#include "../apps/radars/radar_app.h"    // dossier "radars" (pas "radar")
#include "../apps/bourse/bourse_app.h"
#include "../apps/meteo/meteo_app.h"

#define APP_COUNT 4
#define LONG_MS   800

struct AppEntry {
    const char *label;
    const char *sub;
    uint32_t    color_bg;
    uint32_t    color_txt;
    const char *icon;
    void (*launch)();
};

static const AppEntry APPS[APP_COUNT] = {
    { "Nestor",  "IA Compagnon",     0x0D1B3E, 0x7EB8F7, LV_SYMBOL_WIFI,    nestor_app_start  },
    { "Radars",  "Alertes routieres",0x0A0A1A, 0x7EB8F7, LV_SYMBOL_AUDIO,   radar_app_start   },
    { "Bourse",  "Marches & Actifs", 0x071A07, 0x66EE88, LV_SYMBOL_UP,      bourse_app_start  },
    { "Meteo",   "Previsions",       0x0A0E1A, 0xFFCC44, LV_SYMBOL_WARNING, meteo_app_start   },
};

static lv_obj_t *scr_launcher = nullptr;
static lv_obj_t *tileview     = nullptr;
static lv_obj_t *tiles[APP_COUNT];
static int8_t    cur_idx      = 0;

// ── Boutons physiques ─────────────────────────────────────────────────
struct BtnState { uint8_t pin; bool last; bool pressed; uint32_t t0; bool lf; };
static BtnState btnLeft  = {BTN_LEFT,  true, false, 0, false};
static BtnState btnRight = {BTN_RIGHT, true, false, 0, false};

static void go_to(int8_t idx) {
    cur_idx = (idx + APP_COUNT) % APP_COUNT;
    lv_obj_set_tile_id(tileview, cur_idx, 0, LV_ANIM_ON);
}

static void open_current() {
    if (APPS[cur_idx].launch) APPS[cur_idx].launch();
}

static void on_tile_changed(lv_event_t *e) {
    lv_obj_t *tv = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *active = (lv_obj_t *)lv_tileview_get_tile_active(tv);
    cur_idx = (int8_t)lv_obj_get_index(active);
}

static void poll_btn(BtnState &b, void(*on_s)(), void(*on_l)()) {
    bool raw = digitalRead(b.pin);
    if (raw != b.last) {
        b.last = raw;
        if (!raw) { b.pressed = true; b.t0 = millis(); b.lf = false; }
        else       { if (b.pressed && !b.lf && on_s) on_s(); b.pressed = false; }
    }
    if (b.pressed && !b.lf && millis() - b.t0 > LONG_MS) {
        b.lf = true;
        if (on_l) on_l();
    }
}

static void bl_s() { go_to(cur_idx - 1); }
static void br_s() { go_to(cur_idx + 1); }
static void br_l() { open_current(); }

// Appui long BTN_LEFT = retour launcher depuis n'importe quelle app
static void bl_l() {
    if (orchestrator_get_app() != APP_LAUNCHER) {
        ui_launcher_return();
    }
}

static void btn_timer_cb(lv_timer_t *) {
    if (orchestrator_get_app() == APP_LAUNCHER) {
        poll_btn(btnLeft,  bl_s, nullptr);
        poll_btn(btnRight, br_s, br_l);
    } else {
        // Dans une app : BTN_LEFT long = retour au launcher
        poll_btn(btnLeft,  nullptr, bl_l);
        poll_btn(btnRight, nullptr, nullptr);
    }
}

static void make_tile(int i) {
    const AppEntry &a = APPS[i];
    tiles[i] = lv_tileview_add_tile(tileview, i, 0, LV_DIR_HOR);
    lv_obj_set_style_bg_color(tiles[i], lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(tiles[i], LV_OPA_COVER, 0);

    lv_obj_t *card = lv_obj_create(tiles[i]);
    lv_obj_set_size(card, 220, 180);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(a.color_bg), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(a.color_txt), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(card, 30, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(a.color_txt), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);

    lv_obj_t *ico = lv_label_create(card);
    lv_label_set_text(ico, a.icon);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ico, lv_color_hex(a.color_txt), 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, a.label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(a.color_txt), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, a.sub);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(a.color_txt), 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_70, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 52);

    // Indicateur x/4
    lv_obj_t *idx = lv_label_create(tiles[i]);
    char buf[8]; snprintf(buf, sizeof(buf), "%d/%d", i + 1, APP_COUNT);
    lv_label_set_text(idx, buf);
    lv_obj_set_style_text_color(idx, lv_color_hex(0x445566), 0);
    lv_obj_set_style_text_font(idx, &lv_font_montserrat_14, 0);
    lv_obj_align(idx, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void ui_launcher_init() {
    scr_launcher = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_launcher, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr_launcher, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_launcher, LV_OBJ_FLAG_SCROLLABLE);

    tileview = lv_tileview_create(scr_launcher);
    lv_obj_set_size(tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_align(tileview, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tileview, on_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

    for (int i = 0; i < APP_COUNT; i++) make_tile(i);

    lv_obj_t *hint = lv_label_create(scr_launcher);
    lv_label_set_text(hint, "glisser | btn droit long = ouvrir");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x223344), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);  // 10 n'est pas active
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);

    pinMode(BTN_LEFT,  INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    lv_timer_create(btn_timer_cb, 20, NULL);

    lv_scr_load(scr_launcher);
    Serial.println("[UI/LAUNCH] Launcher OK");
}

void ui_launcher_return() {
    orchestrator_set_app(APP_LAUNCHER);
    lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

// ── Menu power (appui long bouton power) ─────────────────────────────
static lv_obj_t *_power_overlay = nullptr;

static void _power_close(lv_event_t *) {
    if (_power_overlay) { lv_obj_del(_power_overlay); _power_overlay = nullptr; }
}
static void _power_sleep(lv_event_t *) {
    if (_power_overlay) { lv_obj_del(_power_overlay); _power_overlay = nullptr; }
    hal_pmu_enter_sleep();
}
static void _power_off(lv_event_t *) {
    if (_power_overlay) { lv_obj_del(_power_overlay); _power_overlay = nullptr; }
    hal_pmu_shutdown();
}

static lv_obj_t *_make_btn(lv_obj_t *parent, const char *label,
                             uint32_t col, lv_event_cb_t cb, int y) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 190, 42);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(col), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, label);
    lv_obj_set_style_text_font(lb, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lb, lv_color_white(), 0);
    lv_obj_center(lb);
    return btn;
}

void ui_power_menu_show() {
    if (_power_overlay) return;

    _power_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_power_overlay, 250, 210);
    lv_obj_align(_power_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_power_overlay, lv_color_hex(0x0D1B2E), 0);
    lv_obj_set_style_bg_opa(_power_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_power_overlay, lv_color_hex(0x2A3A5A), 0);
    lv_obj_set_style_border_width(_power_overlay, 1, 0);
    lv_obj_set_style_radius(_power_overlay, 18, 0);
    lv_obj_clear_flag(_power_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(_power_overlay);
    lv_label_set_text(title, "Alimentation");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    _make_btn(_power_overlay, "Veille",         0x1A3A1A, _power_sleep, 54);
    _make_btn(_power_overlay, "Arret complet",  0x3A1A1A, _power_off,   106);
    _make_btn(_power_overlay, "Annuler",        0x1A1A2A, _power_close, 158);
}
