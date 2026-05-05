#include "meteo_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include <lvgl.h>

static lv_obj_t *_scr_meteo = nullptr;

static void back_cb_meteo(lv_event_t *) {
    orchestrator_set_app(APP_LAUNCHER);
    lv_obj_del(_scr_meteo); _scr_meteo = nullptr;
    ui_launcher_return();
}

void meteo_app_start() {
    orchestrator_set_app(APP_METEO);
    _scr_meteo = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(_scr_meteo, lv_color_hex(0x060E1A), 0);
    lv_obj_set_style_bg_opa(_scr_meteo, LV_OPA_COVER, 0);
    lv_obj_clear_flag(_scr_meteo, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn = lv_btn_create(_scr_meteo);
    lv_obj_set_size(btn, 52, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x060E1A), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, back_cb_meteo, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lb = lv_label_create(btn);
    lv_label_set_text(lb, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lb, lv_color_hex(0xFFCC44), 0);
    lv_obj_center(lb);

    lv_obj_t *title = lv_label_create(_scr_meteo);
    lv_label_set_text(title, "Meteo");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFCC44), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);

    lv_obj_t *body = lv_label_create(_scr_meteo);
    lv_label_set_text(body, "Previsions meteorologiques\n\n(en cours de developpement)");
    lv_obj_set_style_text_font(body, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(body, lv_color_hex(0xFFCC44), 0);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(body, 400);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 0);

    lv_scr_load_anim(_scr_meteo, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
    Serial.println("[APP/METEO] Ouverte");
}

void meteo_app_stop() {
    if (_scr_meteo) { lv_obj_del(_scr_meteo); _scr_meteo = nullptr; }
    orchestrator_set_app(APP_LAUNCHER);
}
