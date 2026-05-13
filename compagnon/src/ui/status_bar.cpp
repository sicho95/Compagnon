#include "status_bar.h"
#include "../hal/pmu.h"
#include "../config/pin_config.h"
#include "../config/ui_config.h"
#include <time.h>
#include <WiFi.h>

#define STATUS_H  36

static lv_obj_t *bar       = nullptr;
static lv_obj_t *lbl_dt    = nullptr;   // date + heure (gauche)
static lv_obj_t *lbl_bt    = nullptr;   // icône BLE
static lv_obj_t *lbl_wifi  = nullptr;   // icône WiFi
static lv_obj_t *bar_bat   = nullptr;   // jauge batterie
static lv_obj_t *lbl_bat   = nullptr;   // % batterie

static bool ntp_done = false;

// Abréviations mois françaises (index 1-12)
static const char *MOIS[] = {
    "", "jan", "fev", "mar", "avr", "mai", "jun",
    "jul", "aou", "sep", "oct", "nov", "dec"
};

void ui_status_bar_init() {
    bar = lv_obj_create(lv_layer_top());
    // La status bar est positionnée dans la safe area horizontale (UI_X, UI_W)
    // et décalée de BORDER_V depuis le haut de l'écran physique.
    lv_obj_set_size(bar, UI_W, STATUS_H);
    lv_obj_set_pos(bar, UI_X, UI_Y);
    lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // ── Gauche : date + heure ─────────────────────────────────────────
    lbl_dt = lv_label_create(bar);
    lv_label_set_text(lbl_dt, "-- --- ---- - --:--");
    lv_obj_set_style_text_color(lbl_dt, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl_dt, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_dt, LV_ALIGN_LEFT_MID, 8, 0);

    // ── Droite : BLE / WiFi / jauge batterie / % ─────────────────────
    lbl_bt = lv_label_create(bar);
    lv_label_set_text(lbl_bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(lbl_bt, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_bt, LV_ALIGN_RIGHT_MID, -114, 0);

    lbl_wifi = lv_label_create(bar);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_wifi, LV_ALIGN_RIGHT_MID, -90, 0);

    // Jauge batterie (28×12 px)
    bar_bat = lv_bar_create(bar);
    lv_obj_set_size(bar_bat, 28, 12);
    lv_obj_align(bar_bat, LV_ALIGN_RIGHT_MID, -56, 0);
    lv_bar_set_range(bar_bat, 0, 100);
    lv_bar_set_value(bar_bat, 50, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_bat, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_bat, lv_color_hex(0x44CC44), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_bat, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_bat, 2, LV_PART_INDICATOR);
    lv_obj_set_style_border_color(bar_bat, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar_bat, 1, LV_PART_MAIN);

    // % batterie
    lbl_bat = lv_label_create(bar);
    lv_label_set_text(lbl_bat, "--%");
    lv_obj_set_style_text_color(lbl_bat, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(lbl_bat, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_bat, LV_ALIGN_RIGHT_MID, -8, 1);
}

void ui_status_bar_set_wifi(bool ok) {
    if (!lbl_wifi) return;
    lv_obj_set_style_text_color(lbl_wifi,
        ok ? lv_color_hex(0x00CC44) : lv_color_hex(0x333333), 0);

    if (ok && !ntp_done) {
        // Fuseau Europe/Paris avec gestion DST automatique
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
        configTime(0, 0, "pool.ntp.org", "time.google.com");
        ntp_done = true;
        Serial.println("[STATUS] NTP configure (Europe/Paris, DST auto)");
    }
}

void ui_status_bar_set_ble(bool ok) {
    if (!lbl_bt) return;
    lv_obj_set_style_text_color(lbl_bt,
        ok ? lv_color_hex(0x7EB8F7) : lv_color_hex(0x333333), 0);
}

void ui_status_bar_tick() {
    static uint32_t last = 0;
    if (millis() - last < 10000 && last) return;
    last = millis();

    // ── Date + heure ──────────────────────────────────────────────────
    if (lbl_dt) {
        struct tm t;
        if (getLocalTime(&t)) {
            char buf[22];
            snprintf(buf, sizeof(buf), "%02d %s %04d - %02d:%02d",
                t.tm_mday,
                (t.tm_mon >= 0 && t.tm_mon < 12) ? MOIS[t.tm_mon + 1] : "???",
                t.tm_myear,            
                t.tm_hour, t.tm_min);
            lv_label_set_text(lbl_dt, buf);
        }
    }

    // ── Batterie ──────────────────────────────────────────────────────
    int pct = hal_pmu_battery_pct();
    if (pct >= 0) {
        if (bar_bat) {
            lv_bar_set_value(bar_bat, pct, LV_ANIM_OFF);
            lv_color_t col = (pct > 30) ? lv_color_hex(0x44CC44)
                           : (pct > 15) ? lv_color_hex(0xF4A236)
                                        : lv_color_hex(0xF44336);
            lv_obj_set_style_bg_color(bar_bat, col, LV_PART_INDICATOR);
        }
        if (lbl_bat) {
            char buf[6];
            snprintf(buf, sizeof(buf), "%d%%", pct);
            lv_label_set_text(lbl_bat, buf);
        }
    }
}
