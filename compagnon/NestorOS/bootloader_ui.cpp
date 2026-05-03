/**
 * bootloader_ui.cpp
 * Launcher « Compagnon » — carousel tactile LVGL v9
 * Grandes cartes défilantes horizontalement, animation snap.
 */
#include "bootloader_ui.h"
#include <lvgl.h>
#include <Arduino.h>
#include "nestor_app.h"
#include "radar_app.h"
#include "lora_app.h"
#include "histoire_app.h"

// ── Palette ─────────────────────────────────────────────────────────────────
#define C_BG      0x06060F
#define C_CARD    0x12152A
#define C_CARD_HL 0x1E2448
#define C_ACCENT  0x7EB8F7
#define C_TEXT    0xE8EAF6
#define C_SUB     0x6B75A0
#define C_BORDER  0x2A3060

// ── Descripteurs des apps ───────────────────────────────────────────────────
typedef struct {
  const char *name;
  const char *subtitle;
  const char *detail;
  const char *icon;
  void (*launch)(void);
} AppCard;

static const AppCard APPS[] = {
  { "Nestor",     "Compagnon IA",  "Chat vocal & texte",   LV_SYMBOL_CALL,  nestor_app_start   },
  { "Radar",      "Scan RF",       "Détection BLE / WiFi", LV_SYMBOL_WIFI,  radar_app_start    },
  { "LoRa / GPS", "Tracker",       "Maillage longue portée",LV_SYMBOL_GPS,  lora_app_start     },
  { "Histoire",   "Lecture IA",    "Narration interactive",LV_SYMBOL_FILE,  histoire_app_start },
};
#define APP_COUNT (sizeof(APPS)/sizeof(APPS[0]))

// ── État global ─────────────────────────────────────────────────────────────
static lv_obj_t *scr_launcher = NULL;
static lv_obj_t *carousel     = NULL;

// ── Callback tap sur une carte ───────────────────────────────────────────────
static void card_tap_cb(lv_event_t *e) {
  uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  Serial.printf("[COMPAGNON] Lancement → %s\n", APPS[idx].name);
  APPS[idx].launch();
}

// ── Construction d'une carte ─────────────────────────────────────────────────
static void make_card(lv_obj_t *parent, uint8_t idx) {
  // Carte principale
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 340, 360);
  lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD_HL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_set_scroll_dir(card, LV_DIR_NONE);
  lv_obj_add_event_cb(card, card_tap_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)idx);

  // Cercle icône
  lv_obj_t *ico_bg = lv_obj_create(card);
  lv_obj_set_size(ico_bg, 110, 110);
  lv_obj_set_style_radius(ico_bg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ico_bg, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_bg_opa(ico_bg, 25, 0);   // très léger
  lv_obj_set_style_border_color(ico_bg, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_border_width(ico_bg, 2, 0);
  lv_obj_set_style_pad_all(ico_bg, 0, 0);
  lv_obj_set_scroll_dir(ico_bg, LV_DIR_NONE);
  lv_obj_align(ico_bg, LV_ALIGN_TOP_MID, 0, 40);

  lv_obj_t *ico = lv_label_create(ico_bg);
  lv_label_set_text(ico, APPS[idx].icon);
  lv_obj_set_style_text_color(ico, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
  lv_obj_center(ico);

  // Nom
  lv_obj_t *lbl_name = lv_label_create(card);
  lv_label_set_text(lbl_name, APPS[idx].name);
  lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_name, lv_color_hex(C_TEXT), 0);
  lv_obj_align(lbl_name, LV_ALIGN_TOP_MID, 0, 170);

  // Sous-titre
  lv_obj_t *lbl_sub = lv_label_create(card);
  lv_label_set_text(lbl_sub, APPS[idx].subtitle);
  lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_sub, lv_color_hex(C_ACCENT), 0);
  lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 206);

  // Détail
  lv_obj_t *lbl_det = lv_label_create(card);
  lv_label_set_text(lbl_det, APPS[idx].detail);
  lv_obj_set_style_text_font(lbl_det, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_det, lv_color_hex(C_SUB), 0);
  lv_obj_set_style_text_align(lbl_det, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lbl_det, 280);
  lv_obj_align(lbl_det, LV_ALIGN_TOP_MID, 0, 238);

  // Bouton « Ouvrir »
  lv_obj_t *btn = lv_btn_create(card);
  lv_obj_set_size(btn, 200, 48);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -24);
  lv_obj_set_style_bg_color(btn, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_radius(btn, 24, 0);
  lv_obj_add_event_cb(btn, card_tap_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)idx);

  lv_obj_t *lbl_btn = lv_label_create(btn);
  lv_label_set_text(lbl_btn, "Ouvrir");
  lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x050510), 0);
  lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_16, 0);
  lv_obj_center(lbl_btn);
}

// ── Construction du launcher ─────────────────────────────────────────────────
void bootloader_ui_show() {
  scr_launcher = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_launcher, lv_color_hex(C_BG), 0);
  lv_obj_set_style_pad_all(scr_launcher, 0, 0);

  // Titre OS
  lv_obj_t *title = lv_label_create(scr_launcher);
  lv_label_set_text(title, "Compagnon");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(C_ACCENT), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

  lv_obj_t *sub = lv_label_create(scr_launcher);
  lv_label_set_text(sub, "Glissez pour choisir");
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(C_SUB), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 48);

  // Carousel horizontal avec snap
  carousel = lv_obj_create(scr_launcher);
  lv_obj_set_size(carousel, 480, 400);
  lv_obj_align(carousel, LV_ALIGN_BOTTOM_MID, 0, -20);
  lv_obj_set_style_bg_opa(carousel, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(carousel, 0, 0);
  lv_obj_set_style_pad_left(carousel, 70, 0);   // marge pour centrer 1ère carte
  lv_obj_set_style_pad_right(carousel, 70, 0);
  lv_obj_set_style_pad_gap(carousel, 24, 0);
  lv_obj_set_scroll_dir(carousel, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(carousel, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(carousel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_flex_flow(carousel, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(carousel, LV_FLEX_ALIGN_START,
                         LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  for (uint8_t i = 0; i < APP_COUNT; i++) {
    make_card(carousel, i);
  }

  // Scroll snap vers la 1ère carte
  lv_obj_scroll_to_x(carousel, 0, LV_ANIM_OFF);

  lv_scr_load(scr_launcher);
}

void bootloader_ui_return() {
  lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}
