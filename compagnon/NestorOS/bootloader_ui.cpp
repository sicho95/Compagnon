/**
 * bootloader_ui.cpp
 * Launcher "Compagnon" — carousel tactile LVGL v9.
 *
 * Corrections v2 :
 *  - LV_OBJ_FLAG_SNAPPABLE sur chaque carte → snap scroll center
 *  - LV_OBJ_FLAG_EVENT_BUBBLE sur les cartes → le swipe passe au carousel
 *  - LV_OBJ_FLAG_GESTURE_BUBBLE sur tous les enfants de carte
 *  - scroll_elastic + scroll_momentum activés sur le carousel
 *  - bootloader_ui_next() / prev() → navigation boutons physiques
 */
#include "bootloader_ui.h"
#include <lvgl.h>
#include <Arduino.h>
#include "nestor_app.h"
#include "radar_app.h"
#include "lora_app.h"
#include "histoire_app.h"

// ── Palette ─────────────────────────────────────────────────────
#define C_BG      0x06060F
#define C_CARD    0x12152A
#define C_CARD_HL 0x1E2448
#define C_ACCENT  0x7EB8F7
#define C_TEXT    0xE8EAF6
#define C_SUB     0x6B75A0
#define C_BORDER  0x2A3060

typedef struct {
  const char *name;
  const char *subtitle;
  const char *detail;
  const char *icon;
  void (*launch)(void);
} AppCard;

static const AppCard APPS[] = {
  { "Nestor",     "Compagnon IA",   "Chat vocal & texte",    LV_SYMBOL_CALL,  nestor_app_start   },
  { "Radar",      "Scan RF",        "Detection BLE / WiFi",  LV_SYMBOL_WIFI,  radar_app_start    },
  { "LoRa / GPS", "Tracker",        "Maillage longue portee",LV_SYMBOL_GPS,   lora_app_start     },
  { "Histoire",   "Lecture IA",     "Narration interactive", LV_SYMBOL_FILE,  histoire_app_start },
};
#define APP_COUNT (sizeof(APPS)/sizeof(APPS[0]))

static lv_obj_t *scr_launcher = NULL;
static lv_obj_t *carousel     = NULL;
static lv_obj_t *cards[APP_COUNT];   // références pour next/prev
static int8_t    cur_idx      = 0;

// ── Callback tap ────────────────────────────────────────────────
static void card_tap_cb(lv_event_t *e) {
  uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  Serial.printf("[COMPAGNON] Lancement -> %s\n", APPS[idx].name);
  APPS[idx].launch();
}

// ── Propagation gesture sur tous les enfants ────────────────────
// Parcourt récursivement les enfants et active EVENT_BUBBLE + GESTURE_BUBBLE
static void make_children_bubble(lv_obj_t *obj) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
  uint32_t cnt = lv_obj_get_child_count(obj);
  for (uint32_t i = 0; i < cnt; i++) {
    make_children_bubble(lv_obj_get_child(obj, i));
  }
}

// ── Construction d'une carte ────────────────────────────────────
static lv_obj_t* make_card(lv_obj_t *parent, uint8_t idx) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_set_size(card, 300, 360);
  lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD_HL), LV_STATE_PRESSED);
  lv_obj_set_style_radius(card, 28, 0);
  lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 0, 0);

  // !! CLÉS DU SCROLL TACTILE !!
  // La carte est snappable (le carousel snappera sur elle)
  lv_obj_add_flag(card, LV_OBJ_FLAG_SNAPPABLE);
  // Le swipe remonte au parent (carousel) même si on commence sur la carte
  lv_obj_add_flag(card, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);
  // La carte elle-même n'est PAS scrollable
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(card, LV_DIR_NONE);

  // Tap pour lancer l'app (sur le bouton Ouvrir principalement)
  lv_obj_add_event_cb(card, card_tap_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)idx);

  // ── Cercle icône ─────────────────────────────────────────────
  lv_obj_t *ico_bg = lv_obj_create(card);
  lv_obj_set_size(ico_bg, 100, 100);
  lv_obj_set_style_radius(ico_bg, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(ico_bg, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_bg_opa(ico_bg, 28, 0);
  lv_obj_set_style_border_color(ico_bg, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_border_width(ico_bg, 2, 0);
  lv_obj_set_style_pad_all(ico_bg, 0, 0);
  lv_obj_clear_flag(ico_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ico_bg, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_align(ico_bg, LV_ALIGN_TOP_MID, 0, 36);

  lv_obj_t *ico = lv_label_create(ico_bg);
  lv_label_set_text(ico, APPS[idx].icon);
  lv_obj_set_style_text_color(ico, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_text_font(ico, &lv_font_montserrat_24, 0);
  lv_obj_add_flag(ico, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_center(ico);

  // ── Nom ───────────────────────────────────────────────────────
  lv_obj_t *lbl_name = lv_label_create(card);
  lv_label_set_text(lbl_name, APPS[idx].name);
  lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_name, lv_color_hex(C_TEXT), 0);
  lv_obj_add_flag(lbl_name, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_align(lbl_name, LV_ALIGN_TOP_MID, 0, 158);

  // ── Sous-titre ────────────────────────────────────────────────
  lv_obj_t *lbl_sub = lv_label_create(card);
  lv_label_set_text(lbl_sub, APPS[idx].subtitle);
  lv_obj_set_style_text_font(lbl_sub, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_sub, lv_color_hex(C_ACCENT), 0);
  lv_obj_add_flag(lbl_sub, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_align(lbl_sub, LV_ALIGN_TOP_MID, 0, 194);

  // ── Détail ────────────────────────────────────────────────────
  lv_obj_t *lbl_det = lv_label_create(card);
  lv_label_set_text(lbl_det, APPS[idx].detail);
  lv_obj_set_style_text_font(lbl_det, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_det, lv_color_hex(C_SUB), 0);
  lv_obj_set_style_text_align(lbl_det, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lbl_det, 260);
  lv_obj_add_flag(lbl_det, LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_align(lbl_det, LV_ALIGN_TOP_MID, 0, 224);

  // ── Bouton Ouvrir ─────────────────────────────────────────────
  lv_obj_t *btn = lv_btn_create(card);
  lv_obj_set_size(btn, 180, 46);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_set_style_bg_color(btn, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_radius(btn, 23, 0);
  // Bouton "Ouvrir" = point d'entrée click principal
  lv_obj_add_event_cb(btn, card_tap_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)idx);
  // Propagation gesture uniquement (pas EVENT_BUBBLE pour garder le click)
  lv_obj_add_flag(btn, LV_OBJ_FLAG_GESTURE_BUBBLE);

  lv_obj_t *lbl_btn = lv_label_create(btn);
  lv_label_set_text(lbl_btn, "Ouvrir");
  lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x050510), 0);
  lv_obj_set_style_text_font(lbl_btn, &lv_font_montserrat_16, 0);
  lv_obj_add_flag(lbl_btn, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_center(lbl_btn);

  return card;
}

// ── Construction du launcher ────────────────────────────────────
void bootloader_ui_show() {
  scr_launcher = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_launcher, lv_color_hex(C_BG), 0);
  lv_obj_set_style_pad_all(scr_launcher, 0, 0);
  lv_obj_clear_flag(scr_launcher, LV_OBJ_FLAG_SCROLLABLE);

  // Titre
  lv_obj_t *title = lv_label_create(scr_launcher);
  lv_label_set_text(title, "Compagnon");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(C_ACCENT), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);  // 46 = sous la status bar (36px)

  lv_obj_t *sub = lv_label_create(scr_launcher);
  lv_label_set_text(sub, "Glissez pour naviguer");
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(C_SUB), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 78);

  // ── Carousel ─────────────────────────────────────────────────
  carousel = lv_obj_create(scr_launcher);
  lv_obj_set_size(carousel, 480, 400);
  lv_obj_align(carousel, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_opa(carousel, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(carousel, 0, 0);
  lv_obj_set_style_pad_all(carousel, 0, 0);

  // Marges latérales pour centrer visuellement la 1ère carte (480-300)/2 = 90
  lv_obj_set_style_pad_left(carousel, 90, 0);
  lv_obj_set_style_pad_right(carousel, 90, 0);
  lv_obj_set_style_pad_gap(carousel, 20, 0);

  // !! Scroll horizontal avec SNAP CENTER et momentum élastique !!
  lv_obj_set_scroll_dir(carousel, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(carousel, LV_SCROLL_SNAP_CENTER);
  lv_obj_add_flag(carousel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_add_flag(carousel, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_set_scrollbar_mode(carousel, LV_SCROLLBAR_MODE_OFF);

  // Layout flex row
  lv_obj_set_flex_flow(carousel, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(carousel,
    LV_FLEX_ALIGN_START,
    LV_FLEX_ALIGN_CENTER,
    LV_FLEX_ALIGN_CENTER);

  // Création des cartes
  for (uint8_t i = 0; i < APP_COUNT; i++) {
    cards[i] = make_card(carousel, i);
  }

  // Scroll initial sur la 1ère carte
  cur_idx = 0;
  lv_obj_scroll_to_view(cards[0], LV_ANIM_OFF);

  lv_scr_load(scr_launcher);
}

// ── Navigation boutons physiques ────────────────────────────────
void bootloader_ui_next() {
  if (!carousel || !scr_launcher) return;
  if (lv_scr_act() != scr_launcher) return;
  cur_idx = (cur_idx + 1) % APP_COUNT;
  lv_obj_scroll_to_view(cards[cur_idx], LV_ANIM_ON);
}

void bootloader_ui_prev() {
  if (!carousel || !scr_launcher) return;
  if (lv_scr_act() != scr_launcher) return;
  cur_idx = (cur_idx - 1 + APP_COUNT) % APP_COUNT;
  lv_obj_scroll_to_view(cards[cur_idx], LV_ANIM_ON);
}

void bootloader_ui_return() {
  lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}
