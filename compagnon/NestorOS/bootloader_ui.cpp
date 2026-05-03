/**
 * bootloader_ui.cpp
 * Launcher graphique LVGL — thème sombre Nestor.
 * Chaque carte = un écran LVGL distinct, chargé sans reboot.
 */
#include "bootloader_ui.h"
#include <lvgl.h>
#include <Arduino.h>

// Imports des apps
#include "nestor_app.h"
#include "radar_app.h"
#include "lora_app.h"
#include "histoire_app.h"

// ── Palette ─────────────────────────────────────────────────────
#define C_BG       0x0A0A1A
#define C_CARD     0x141830
#define C_ACCENT   0x7EB8F7
#define C_TEXT     0xE8EAF6
#define C_SUBTEXT  0x8890B0

typedef struct {
  const char *name;
  const char *subtitle;
  const char *icon;      // symbole LVGL
  void (*launch)(void);
} AppCard;

static const AppCard APPS[] = {
  { "Nestor",       "Compagnon IA",     LV_SYMBOL_CALL,    nestor_app_start  },
  { "Radar",        "Scan RF",          LV_SYMBOL_WIFI,    radar_app_start   },
  { "LoRa / GPS",   "Tracker",          LV_SYMBOL_GPS,     lora_app_start    },
  { "Histoire",     "Lecture IA",       LV_SYMBOL_FILE,    histoire_app_start},
};
#define APP_COUNT (sizeof(APPS)/sizeof(APPS[0]))

static lv_obj_t *scr_launcher = NULL;

// ── Callback bouton ─────────────────────────────────────────────
static void card_cb(lv_event_t *e) {
  uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  Serial.printf("[LAUNCHER] → %s\n", APPS[idx].name);
  APPS[idx].launch();
}

// ── Construction UI ─────────────────────────────────────────────
void bootloader_ui_show() {
  scr_launcher = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_launcher, lv_color_hex(C_BG), 0);

  // Titre
  lv_obj_t *title = lv_label_create(scr_launcher);
  lv_label_set_text(title, "NESTOR OS");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(C_ACCENT), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

  // Sous-titre
  lv_obj_t *sub = lv_label_create(scr_launcher);
  lv_label_set_text(sub, "Choisissez une application");
  lv_obj_set_style_text_color(sub, lv_color_hex(C_SUBTEXT), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 50);

  // Cartes d'apps
  for (uint8_t i = 0; i < APP_COUNT; i++) {
    lv_obj_t *card = lv_btn_create(scr_launcher);
    lv_obj_set_size(card, 420, 65);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 85 + i * 80);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_add_event_cb(card, card_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);

    lv_obj_t *ico = lv_label_create(card);
    lv_label_set_text(ico, APPS[i].icon);
    lv_obj_set_style_text_color(ico, lv_color_hex(C_ACCENT), 0);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, APPS[i].name);
    lv_obj_set_style_text_color(lbl, lv_color_hex(C_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 52, -8);

    lv_obj_t *sub2 = lv_label_create(card);
    lv_label_set_text(sub2, APPS[i].subtitle);
    lv_obj_set_style_text_color(sub2, lv_color_hex(C_SUBTEXT), 0);
    lv_obj_align(sub2, LV_ALIGN_LEFT_MID, 52, 10);
  }

  lv_scr_load(scr_launcher);
}

void bootloader_ui_return() {
  lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}
