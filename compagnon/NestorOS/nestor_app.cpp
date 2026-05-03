#include "nestor_app.h"
#include "bootloader_ui.h"
#include "orchestrator.h"
#include "brain.h"
#include <lvgl.h>

static lv_obj_t *scr_nestor = NULL;

static void back_cb(lv_event_t *e) {
  nestor_app_stop();
  bootloader_ui_return();
}

void nestor_app_start() {
  orchestrator_set_app(APP_NESTOR);

  scr_nestor = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_nestor, lv_color_hex(0x0A0A1A), 0);

  // Bouton retour
  lv_obj_t *btn_back = lv_btn_create(scr_nestor);
  lv_obj_set_size(btn_back, 60, 40);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  lv_obj_center(lbl_back);

  // Placeholder UI Nestor
  lv_obj_t *lbl = lv_label_create(scr_nestor);
  lv_label_set_text(lbl, "Nestor\nCompagnon IA");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x7EB8F7), 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

  // TODO : construire l'UI complète (chat, statut, commandes vocales)

  lv_scr_load_anim(scr_nestor, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void nestor_app_stop() {
  orchestrator_set_app(APP_LAUNCHER);
}
