#include "launcher.h"
#include "../config/pin_config.h"
#include "../hal/pmu.h"
#include "../system/orchestrator.h"
#include "../apps/nestor/nestor_app.h"
#include "../apps/radars/radar_app.h"
#include "../apps/bourse/bourse_app.h"
#include "../apps/meteo/meteo_app.h"
#include "../apps/musique/musique_app.h"
#include "../net/ble_mgr.h"

#define APP_COUNT 5
#define LONG_MS   800

struct AppEntry {
    const char *label;
    const char *sub;
    uint32_t    color_bg;
    uint32_t    color_txt;
    const char *icon;
    void (*launch)();
    void (*stop)();   // quitter proprement depuis le bouton matériel
};

// palette AMOLED cohérente avec les apps respectives
static const AppEntry APPS[APP_COUNT] = {
    { "Nestor",  "IA Compagnon",     0x0D1B3E, 0x7EB8F7, LV_SYMBOL_WIFI,    nestor_app_start  },
    { "Radars",  "Alertes routieres",0x0A0A1A, 0x7EB8F7, LV_SYMBOL_AUDIO,   radar_app_start   },
    { "Bourse",  "Marches & Actifs", 0x071A07, 0x66EE88, LV_SYMBOL_UP,      bourse_app_start  },
    { "Meteo",   "Previsions",       0x0A0E1A, 0xFFCC44, LV_SYMBOL_WARNING, meteo_app_start   },
    { "Musique", "AirPlay & SD",     0x1b0030, 0xCC99FF, LV_SYMBOL_AUDIO,   musique_app_start },
};

static lv_obj_t *scr_launcher   = nullptr;
static lv_obj_t *tileview       = nullptr;
static lv_obj_t *tiles[APP_COUNT];
static lv_obj_t *_cards[APP_COUNT];  // cartes pour l'opacité dynamique
static int8_t    cur_idx        = 0;

// ── Bordure de focus via lv_layer_top() ──────────────────────────────────────
// 4 rectangles noirs de 5 px plaqués sur les bords de l'écran
#define BORDER_W 5
static lv_obj_t *_border[4] = {};

// Ramener la bordure au premier plan après tout nouvel overlay
void ui_frame_to_front() {
    for (int i = 0; i < 4; i++) {
        if (_border[i]) lv_obj_move_foreground(_border[i]);
    }
}

static void _border_create() {
    lv_obj_t *top = lv_layer_top();
    // Ordre : haut, bas, gauche, droite
    static const struct { int16_t x, y, w, h; } RECTS[4] = {
        { 0, 0,  480, BORDER_W },
        { 0, 480 - BORDER_W, 480, BORDER_W },
        { 0, 0,  BORDER_W, 480 },
        { 480 - BORDER_W, 0, BORDER_W, 480 },
    };
    for (int i = 0; i < 4; i++) {
        _border[i] = lv_obj_create(top);
        lv_obj_set_pos(_border[i],  RECTS[i].x, RECTS[i].y);
        lv_obj_set_size(_border[i], RECTS[i].w, RECTS[i].h);
        lv_obj_set_style_bg_color(_border[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(_border[i],   LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(_border[i], 0, 0);
        lv_obj_set_style_radius(_border[i],    0, 0);
        lv_obj_clear_flag(_border[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
}

// ── Mise à jour de l'opacité selon la tuile active ───────────────────────────
static void _update_opacity() {
    for (int i = 0; i < APP_COUNT; i++) {
        lv_opa_t opa = (i == cur_idx) ? LV_OPA_70 : LV_OPA_30;
        lv_obj_set_style_bg_opa(cards[i], opa, 0);
    }
}

// ── Boutons physiques ─────────────────────────────────────────────────────────
#define BTN_DEBOUNCE_MS 20

struct BtnState {
    uint8_t  pin;
    bool     stable;
    bool     raw;
    uint32_t edge_t;
    bool     pressed;
    uint32_t press_t;
    bool     long_done;
};

// pin 0  (BTN_RIGHT) = physiquement à gauche → Préc
// pin 18 (BTN_LEFT)  = physiquement à droite → Suiv
static BtnState btnRight = {BTN_RIGHT, true, true, 0, false, 0, false};
static BtnState btnLeft  = {BTN_LEFT,  true, true, 0, false, 0, false};

static void go_to(int8_t idx) {
    cur_idx = (idx + APP_COUNT) % APP_COUNT;
    lv_obj_set_tile_id(tileview, cur_idx, 0, LV_ANIM_ON);
}
static void open_current() {
    if (APPS[cur_idx].launch) APPS[cur_idx].launch();
}
static void stop_active_app() {
    ActiveApp a = orchestrator_get_app();
    for (int i = 0; i < APP_COUNT; i++) {
        const AppEntry &e = APPS[i];
        // Chaque app gère son propre stop via orchestrator_set_app(APP_LAUNCHER)
        // on appelle stop() de l'app correspondant au enum actif
    }
    // Mapping enum → stop()
    switch (a) {
        case APP_NESTOR:  /* nestor_app_stop(); */ break;  // nestor gère l'arrêt via son bouton retour
        case APP_RADAR:   radar_app_stop();   break;
        case APP_BOURSE:  bourse_app_stop();  break;
        case APP_METEO:   meteo_app_stop();   break;
        default: break;
    }
}

// Appelle le stop de l'app active, puis revient au launcher
static void stop_current_and_return() {
    ActiveApp app = orchestrator_get_app();
    if (app == APP_LAUNCHER) return;
    // APP_NESTOR=1..APP_METEO=4 → APPS[0..3]
    int idx = (int)app - 1;
    if (idx >= 0 && idx < APP_COUNT && APPS[idx].stop) APPS[idx].stop();
    ui_launcher_return();
}

// mettre à jour l'opacité des cartes selon la tuile active
static void update_tile_opacities(int8_t focus) {
    for (int i = 0; i < APP_COUNT; i++) {
        if (!_cards[i]) continue;
        lv_obj_set_style_bg_opa(_cards[i],
            i == focus ? LV_OPA_70 : LV_OPA_30, 0);
    }
}

static void on_tile_changed(lv_event_t *e) {
    lv_obj_t *tv     = (lv_obj_t *)lv_event_get_target(e);
    lv_obj_t *active = (lv_obj_t *)lv_tileview_get_tile_active(tv);
    cur_idx = (int8_t)lv_obj_get_index(active);
    update_tile_opacities(cur_idx);
}

static void poll_btn(BtnState &b, void(*on_s)(), void(*on_l)()) {
    bool cur = (bool)digitalRead(b.pin);

    // Toute transition brute redémarre la fenêtre anti-rebond
    if (cur != b.raw) {
        b.raw    = cur;
        b.edge_t = millis();
    }
    if (millis() - b.edge_t < BTN_DEBOUNCE_MS) return;

    // État stabilisé — traiter uniquement si différent du dernier état confirmé
    if (cur == b.stable) {
        if (b.pressed && !b.long_done && millis() - b.press_t >= LONG_MS) {
            b.long_done = true;
            if (on_l) on_l();
        }
        return;
    }

    b.stable = cur;
    if (!cur) {                               // front descendant : appui confirmé
        b.pressed   = true;
        b.press_t   = millis();
        b.long_done = false;
    } else {                                  // front montant : relâchement
        if (b.pressed && !b.long_done && on_s) on_s();
        b.pressed = false;
    }
}

// ── Callbacks bouton Préc (gauche, pin 0) ─────────────────────────────
static void btn_prev_s() {
    if (orchestrator_get_app() == APP_LAUNCHER) go_to(cur_idx - 1);
}
static void btn_prev_l() {
    // Appui long = Retour : arrête proprement l'app et revient au launcher
    stop_current_and_return();
}

// ── Callbacks bouton Suiv (droite, pin 18) ────────────────────────────
static void btn_next_s() {
    if (orchestrator_get_app() == APP_LAUNCHER) go_to(cur_idx + 1);
}
static void btn_next_l() {
    if (orchestrator_get_app() == APP_LAUNCHER) {
        open_current();  // depuis launcher = ouvrir l'app sélectionnée
    } else {
        // Depuis une app : envoyer CLICKED à l'objet focusé (valider/confirmer)
        lv_group_t *g = lv_group_get_default();
        lv_obj_t   *f = g ? lv_group_get_focused(g) : nullptr;
        if (f) lv_event_send(f, LV_EVENT_CLICKED, nullptr);
    }
}

// Appelée en PREMIER dans loop() — hors timer LVGL pour latence minimale
void ui_launcher_btn_tick() {
    poll_btn(btnRight, btn_prev_s, btn_prev_l);  // pin 0  = gauche = Préc
    poll_btn(btnLeft,  btn_next_s, btn_next_l);  // pin 18 = droite = Suiv
}

// ── Bordure noire 5 px sur les 4 bords ───────────────────────────────
void ui_screen_border_init() {
    static const struct { int16_t x; int16_t y; int16_t w; int16_t h; } BANDS[4] = {
        { 0,              0,               LCD_WIDTH,  5          },
        { 0,              LCD_HEIGHT - 5,  LCD_WIDTH,  5          },
        { 0,              0,               5,          LCD_HEIGHT },
        { LCD_WIDTH - 5,  0,               5,          LCD_HEIGHT },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_obj_create(lv_layer_top());
        lv_obj_set_pos(b, BANDS[i].x, BANDS[i].y);
        lv_obj_set_size(b, BANDS[i].w, BANDS[i].h);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_radius(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    }
}

// ── Bordure noire pour boîtier arrondi ────────────────────────────────────────
static void add_screen_border() {
    const int B = BORDER_PX;
    lv_obj_t *sys = lv_layer_sys();

    auto mk = [&](int x, int y, int w, int h) {
        lv_obj_t *r = lv_obj_create(sys);
        lv_obj_set_pos(r, x, y);
        lv_obj_set_size(r, w, h);
        lv_obj_set_style_bg_color(r, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(r, 0, 0);
        lv_obj_set_style_radius(r, 0, 0);
        lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    };

    mk(0,                  0,           LCD_WIDTH,  B);           // haut
    mk(0,                  LCD_HEIGHT-B, LCD_WIDTH, B);           // bas
    mk(0,                  0,           B,           LCD_HEIGHT); // gauche
    mk(LCD_WIDTH-B,        0,           B,           LCD_HEIGHT); // droite
}

// ── Construction d'une tuile ──────────────────────────────────────────────────
static void make_tile(int i) {
    const AppEntry &a = APPS[i];
    tiles[i] = lv_tileview_add_tile(tileview, i, 0, LV_DIR_HOR);
    // Fond noir : seule la carte centrale est colorée
    lv_obj_set_style_bg_color(tiles[i], lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tiles[i], LV_OPA_COVER, 0);

    // Carte centrale
    lv_obj_t *card = lv_obj_create(tiles[i]);
    lv_obj_set_size(card, 220, 180);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    // Opacité pleine pour que la couleur de l'app soit bien visible
    lv_obj_set_style_bg_color(card, lv_color_hex(a.color_bg), 0);
    // opacité mise à jour dynamiquement (30 = voisin, 70 = focus)
    lv_obj_set_style_bg_opa(card, LV_OPA_30, 0);
    _cards[i] = card;
    lv_obj_set_style_border_color(card, lv_color_hex(a.color_txt), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_shadow_width(card, 30, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(a.color_txt), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);

    lv_obj_t *ico = lv_label_create(card);
    lv_label_set_text(ico, a.icon);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ico, lv_color_hex(a.color_txt), 0);
    lv_obj_align(ico, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, a.label);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(a.color_txt), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);

    lv_obj_t *sub = lv_label_create(card);
    lv_label_set_text(sub, a.sub);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(a.color_txt), 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_60, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 52);

    // Indicateur x/4
    lv_obj_t *idx_lbl = lv_label_create(tiles[i]);
    char buf[8]; snprintf(buf, sizeof(buf), "%d/%d", i + 1, APP_COUNT);
    lv_label_set_text(idx_lbl, buf);
    lv_obj_set_style_text_color(idx_lbl, lv_color_hex(0x334455), 0);
    lv_obj_set_style_text_font(idx_lbl, &lv_font_montserrat_12, 0);
    lv_obj_align(idx_lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
}

void ui_launcher_init() {
    scr_launcher = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_launcher, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr_launcher, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr_launcher, LV_OBJ_FLAG_SCROLLABLE);

    tileview = lv_tileview_create(scr_launcher);
    lv_obj_set_size(tileview, LV_PCT(100), LV_PCT(100));
    lv_obj_align(tileview, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(tileview, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tileview, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tileview, on_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

    for (int i = 0; i < APP_COUNT; i++) make_tile(i);

    lv_obj_t *hint = lv_label_create(scr_launcher);
    lv_label_set_text(hint, "glisser | btn droit long = ouvrir");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x1A2233), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -2);

    pinMode(BTN_LEFT,  INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);

    // Relier les commandes BLE music: à l'app Musique
    ble_mgr_set_music_cb(musique_ble_cmd);

    lv_scr_load(scr_launcher);
    add_screen_border();   // bordure noire par-dessus tout (lv_layer_sys)

    Serial.println("[UI/LAUNCH] Launcher OK");
}

void ui_launcher_return() {
    orchestrator_set_app(APP_LAUNCHER);
    lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

// ── Menu power (appui long bouton power PMU) ──────────────────────────────────
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
    lv_obj_set_style_bg_color(_power_overlay, lv_color_hex(0x080810), 0);
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

    // Bordure écran toujours au premier plan, même au-dessus de l'overlay power
    ui_frame_to_front();
}
