#include "nestor_app.h"
#include "../../ui/launcher.h"
#include "../../system/orchestrator.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WebServer.h>

#define PWA_URL           "https://sicho95.github.io/Nestor/"
#define WIFI_TIMEOUT_MS   12000

static lv_obj_t *scr        = nullptr;
static lv_obj_t *_bubble    = nullptr;  // bulle centrale bleu nuit
static lv_obj_t *lbl_status = nullptr;
static lv_obj_t *lbl_url    = nullptr;
static lv_obj_t *spinner    = nullptr;
static WebServer *srv       = nullptr;

static void back_cb(lv_event_t *) { nestor_app_stop(); ui_launcher_return(); }

static void set_status(const char *msg, uint32_t col) {
    if (lbl_status) {
        lv_label_set_text(lbl_status, msg);
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(col), 0);
    }
}

static void build_ui() {
    scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // bouton retour — fond noir pour ne pas gâcher de pixels
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 52, 36);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 46);
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_b = lv_label_create(btn);
    lv_label_set_text(lbl_b, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(lbl_b, lv_color_hex(0x7EB8F7), 0);
    lv_obj_center(lbl_b);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Nestor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x7EB8F7), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 46);

    // bulle centrale — bleu nuit semi-transparent
    _bubble = lv_obj_create(scr);
    lv_obj_set_size(_bubble, 380, 180);
    lv_obj_align(_bubble, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(_bubble, lv_color_hex(0x1a237e), 0);
    lv_obj_set_style_bg_opa(_bubble, LV_OPA_50, 0);
    lv_obj_set_style_border_width(_bubble, 0, 0);
    lv_obj_set_style_radius(_bubble, 16, 0);
    lv_obj_clear_flag(_bubble, LV_OBJ_FLAG_SCROLLABLE);

    spinner = lv_spinner_create(_bubble);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -40);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x7EB8F7), LV_PART_INDICATOR);

    lbl_status = lv_label_create(_bubble);
    lv_label_set_text(lbl_status, "Connexion WiFi...");
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xE0E0E0), 0);
    lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 42);

    lbl_url = lv_label_create(_bubble);
    lv_label_set_text(lbl_url, "");
    lv_obj_set_style_text_font(lbl_url, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_url, lv_color_hex(0xB0BEC5), 0);
    lv_obj_set_style_text_align(lbl_url, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(lbl_url, 340);
    lv_obj_align(lbl_url, LV_ALIGN_CENTER, 0, 72);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

static void wifi_task(void *) {
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) delay(200);
    if (WiFi.status() == WL_CONNECTED) {
        srv = new WebServer(80);
        srv->on("/", []() { srv->sendHeader("Location", PWA_URL, true); srv->send(302, "text/plain", ""); });
        srv->begin();
        lv_async_call([](void *) {
            if (spinner) lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
            set_status("Ouvrez sur votre telephone :", 0x4CAF50);
            if (lbl_url) lv_label_set_text(lbl_url, PWA_URL);
        }, nullptr);
    } else {
        lv_async_call([](void *) {
            if (spinner) lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
            set_status("WiFi indisponible\nNestorOS_Setup", 0xF44336);
        }, nullptr);
    }
    vTaskDelete(NULL);
}

void nestor_app_start() {
    orchestrator_set_app(APP_NESTOR);
    build_ui();
    // Core 1 = réseau — évite de bloquer LVGL sur Core 0
    xTaskCreatePinnedToCore(wifi_task, "nestor_wifi", 4096, NULL, 1, NULL, 1);
}

void nestor_app_stop() {
    if (srv) { srv->stop(); delete srv; srv = nullptr; }
    lbl_status = nullptr; lbl_url = nullptr; spinner = nullptr; _bubble = nullptr;
    orchestrator_set_app(APP_LAUNCHER);
}
