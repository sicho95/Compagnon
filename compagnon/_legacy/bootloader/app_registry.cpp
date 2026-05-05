#include "app_registry.h"
#include <Arduino.h>

// ── UI Bootloader ────────────────────────────────────────────────────────────
static const AppEntry *_apps;
static uint8_t         _count;

static void btn_event_cb(lv_event_t *e) {
  uint8_t idx = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  Serial.printf("Lancement app : %s\n", _apps[idx].name);
  _apps[idx].launch();
}

void bootloader_ui_create(const AppEntry *apps, uint8_t count) {
  _apps  = apps;
  _count = count;

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  // Titre
  lv_obj_t *title = lv_label_create(scr);
  lv_label_set_text(title, "NESTOR");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x7EB8F7), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

  // Grille de boutons
  for (uint8_t i = 0; i < count; i++) {
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 70);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 70 + i * 85);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A2540), 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_add_event_cb(btn, btn_event_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)i);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text_fmt(lbl, "%s  %s", apps[i].icon, apps[i].name);
    lv_obj_center(lbl);
  }
}

// ── Lanceurs ─────────────────────────────────────────────────────────────────
void app_launch_nestor(void) {
  // TODO : charger nestor_app/main.ino via dynamic dispatch ou reboot
  Serial.println("[BOOT] Nestor compagnon...");
}

void app_launch_placeholder(void) {
  Serial.println("[BOOT] App non disponible dans cette version.");
  // Afficher un écran 'coming soon'
}
