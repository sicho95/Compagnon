/**
 * status_bar.cpp
 * Barre d'état persistante 480×40 px — layer système LVGL.
 * Affiche : heure (NTP), batterie AXP2101, icône WiFi, icône BT.
 *
 * Fix v2 :
 *  - SB_HEIGHT 36 -> 40 (plus visible)
 *  - NTP configuré ici (configTime) avec fallback heure locale
 *  - Heure affichée même sans WiFi (RTC interne)
 *  - lbl_batt lié à pmu_get_battery_percent() dans pwr_button.cpp
 */
#include "status_bar.h"
#include "pin_config.h"
#include <lvgl.h>
#include <WiFi.h>
#include <time.h>
#include <Arduino.h>

#define SB_BG       0x08081A
#define SB_TEXT     0xC8CCDC
#define SB_ACCENT   0x7EB8F7
#define SB_WARN     0xF4A236
#define SB_OK       0x4CAF50
#define SB_OFF      0x404060

#define SB_HEIGHT   40

static lv_obj_t *bar       = NULL;
static lv_obj_t *lbl_time  = NULL;
static lv_obj_t *lbl_batt  = NULL;
static lv_obj_t *lbl_wifi  = NULL;
static lv_obj_t *lbl_bt    = NULL;

static uint32_t last_refresh = 0;
static bool     ntp_sync     = false;

extern int  pmu_get_battery_percent();
extern bool bt_is_active();

void status_bar_init() {
  // Configure NTP (sync quand WiFi sera dispo)
  configTime(3600, 3600, "pool.ntp.org", "time.google.com"); // UTC+1 + DST

  bar = lv_obj_create(lv_layer_sys());
  lv_obj_set_size(bar, LV_HOR_RES, SB_HEIGHT);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(SB_BG), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 12, 0);
  lv_obj_set_style_pad_ver(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // Heure (gauche)
  lbl_time = lv_label_create(bar);
  lv_label_set_text(lbl_time, "--:--");
  lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_time, lv_color_hex(SB_TEXT), 0);
  lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 0, 0);

  // BT (droite)
  lbl_bt = lv_label_create(bar);
  lv_label_set_text(lbl_bt, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(lbl_bt, lv_color_hex(SB_OFF), 0);
  lv_obj_align(lbl_bt, LV_ALIGN_RIGHT_MID, 0, 0);

  // WiFi
  lbl_wifi = lv_label_create(bar);
  lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(SB_OFF), 0);
  lv_obj_align(lbl_wifi, LV_ALIGN_RIGHT_MID, -34, 0);

  // Batterie
  lbl_batt = lv_label_create(bar);
  lv_label_set_text(lbl_batt, "?%");
  lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_batt, lv_color_hex(SB_TEXT), 0);
  lv_obj_align(lbl_batt, LV_ALIGN_RIGHT_MID, -72, 0);
}

void status_bar_tick() {
  if (!bar) return;
  uint32_t now = millis();
  if (now - last_refresh < 5000) return;
  last_refresh = now;

  // Heure : NTP si WiFi sinon RTC interne
  struct tm ti;
  bool got_time = getLocalTime(&ti, 10); // 10 ms timeout
  if (got_time) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text(lbl_time, buf);
  }
  // Si pas encore synchro NTP et WiFi dispo, retente configTime
  if (!ntp_sync && WiFi.status() == WL_CONNECTED) {
    configTime(3600, 3600, "pool.ntp.org", "time.google.com");
    ntp_sync = true;
  }

  // WiFi
  lv_obj_set_style_text_color(lbl_wifi,
    WiFi.status() == WL_CONNECTED
      ? lv_color_hex(SB_OK)
      : lv_color_hex(SB_OFF), 0);

  // BT
  lv_obj_set_style_text_color(lbl_bt,
    bt_is_active()
      ? lv_color_hex(SB_ACCENT)
      : lv_color_hex(SB_OFF), 0);

  // Batterie
  int pct = pmu_get_battery_percent();
  if (pct >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(lbl_batt, buf);
    uint32_t col = pct > 30 ? SB_OK : (pct > 15 ? SB_WARN : 0xF44336);
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(col), 0);
  }
}
