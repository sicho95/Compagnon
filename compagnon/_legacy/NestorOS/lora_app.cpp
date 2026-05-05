#include "lora_app.h"
#include "bootloader_ui.h"
#include "orchestrator.h"
#include <lvgl.h>

static void back_cb(lv_event_t *e) { bootloader_ui_return(); }

void lora_app_start() {
  orchestrator_set_app(APP_LORA);
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A1A), 0);

  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, "LoRa / GPS\n(à venir)");
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x7EB8F7), 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

  lv_obj_t *btn = lv_btn_create(scr);
  lv_obj_set_size(btn, 60, 40);
  lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *l = lv_label_create(btn);
  lv_label_set_text(l, LV_SYMBOL_LEFT);
  lv_obj_center(l);

  lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
