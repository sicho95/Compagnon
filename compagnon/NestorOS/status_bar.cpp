/**
 * status_bar.cpp
 * Barre d'état persistante 480×36 px — layer système LVGL.
 * Affiche : heure (NTP), batterie AXP2101, icône WiFi, icône BT.
 * Reste visible par-dessus toutes les applications.
 */
#include "status_bar.h"
#include "pin_config.h"
#include <lvgl.h>
#include <WiFi.h>
#include <time.h>
#include <Arduino.h>

// ── Palette ─────────────────────────────────────────────────────
#define SB_BG       0x08081A
#define SB_TEXT     0xC8CCDC
#define SB_ACCENT   0x7EB8F7
#define SB_WARN     0xF4A236
#define SB_OK       0x4CAF50
#define SB_OFF      0x404060

#define SB_HEIGHT   36

static lv_obj_t *bar       = NULL;
static lv_obj_t *lbl_time  = NULL;
static lv_obj_t *lbl_batt  = NULL;
static lv_obj_t *lbl_wifi  = NULL;
static lv_obj_t *lbl_bt    = NULL;

static uint32_t last_refresh = 0;

// ── Lecture batterie AXP2101 (I2C déjà init dans display_init) ───
// On interroge directement le registre si XPowersLib n'est pas
// accessible depuis ici. Sinon, appelle pmu.getBatteryPercent().
extern int pmu_get_battery_percent();  // déclaré dans display_init.cpp

// ── Init ─────────────────────────────────────────────────────────
void status_bar_init() {
  // Crée la barre sur le layer système (au-dessus de tout)
  bar = lv_obj_create(lv_layer_sys());
  lv_obj_set_size(bar, LV_HOR_RES, SB_HEIGHT);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(SB_BG), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_90, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  // ── Heure (gauche) ──────────────────────────────────────────
  lbl_time = lv_label_create(bar);
  lv_label_set_text(lbl_time, "--:--");
  lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_time, lv_color_hex(SB_TEXT), 0);
  lv_obj_align(lbl_time, LV_ALIGN_LEFT_MID, 12, 0);

  // ── BT (droite - 3e icône) ─────────────────────────────────
  lbl_bt = lv_label_create(bar);
  lv_label_set_text(lbl_bt, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(lbl_bt, lv_color_hex(SB_OFF), 0);
  lv_obj_align(lbl_bt, LV_ALIGN_RIGHT_MID, -12, 0);

  // ── WiFi (droite - 2e icône) ───────────────────────────────
  lbl_wifi = lv_label_create(bar);
  lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(SB_OFF), 0);
  lv_obj_align(lbl_wifi, LV_ALIGN_RIGHT_MID, -44, 0);

  // ── Batterie (droite - 1ère) ───────────────────────────────
  lbl_batt = lv_label_create(bar);
  lv_label_set_text(lbl_batt, "?%");
  lv_obj_set_style_text_font(lbl_batt, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_batt, lv_color_hex(SB_TEXT), 0);
  lv_obj_align(lbl_batt, LV_ALIGN_RIGHT_MID, -80, 0);
}

// ── Tick (appelé dans loop, toutes les 5 s max) ──────────────────
void status_bar_tick() {
  if (!bar) return;
  uint32_t now = millis();
  if (now - last_refresh < 5000) return;
  last_refresh = now;

  // ── Heure NTP ──────────────────────────────────────────────
  struct tm ti;
  if (WiFi.status() == WL_CONNECTED && getLocalTime(&ti)) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text(lbl_time, buf);
  }

  // ── WiFi ───────────────────────────────────────────────────
  lv_obj_set_style_text_color(lbl_wifi,
    WiFi.status() == WL_CONNECTED
      ? lv_color_hex(SB_OK)
      : lv_color_hex(SB_OFF), 0);

  // ── BT ─────────────────────────────────────────────────────
  // BluetoothSerial::isReady() n'est pas dispo partout ;
  // on colorie en accent si BT a été activé dans bt_manager
  extern bool bt_is_active();  // défini dans bt_manager.cpp
  lv_obj_set_style_text_color(lbl_bt,
    bt_is_active()
      ? lv_color_hex(SB_ACCENT)
      : lv_color_hex(SB_OFF), 0);

  // ── Batterie ───────────────────────────────────────────────
  int pct = pmu_get_battery_percent();
  if (pct >= 0) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    lv_label_set_text(lbl_batt, buf);
    uint32_t col = pct > 30 ? SB_OK : (pct > 15 ? SB_WARN : 0xF44336);
    lv_obj_set_style_text_color(lbl_batt, lv_color_hex(col), 0);
  }
}
