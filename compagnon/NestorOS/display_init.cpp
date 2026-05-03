/**
 * display_init.cpp
 * Branche la lib Mylibrary (Waveshare) sur LVGL.
 *
 * ⚠️  La lib Waveshare "Mylibrary" doit être installée dans
 *     Documents/Arduino/libraries/  (récupérée depuis le repo
 *     waveshareteam/ESP32-S3-Touch-AMOLED-2.16 → examples/Arduino-v3.3.5/libraries/Mylibrary)
 */
#include "display_init.h"

// ── Waveshare driver ────────────────────────────────────────────
// Décommente la ligne correspondant à ta carte une fois la lib installée :
// #include <ESP32_S3_AMOLED.h>   // nom exact selon Mylibrary

// ── Tampon LVGL (1/10 écran) ────────────────────────────────────
static lv_color_t   buf[SCREEN_W * SCREEN_H / 10];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t      disp_drv;
static lv_indev_drv_t     indev_drv;

// ── Callbacks LVGL ──────────────────────────────────────────────
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p) {
  // TODO : appel driver Waveshare → tft.pushImageDMA(...)
  // Exemple : tft.drawBitmap(area->x1, area->y1, ...);
  lv_disp_flush_ready(drv);
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  // TODO : lire le touch via Mylibrary
  // Exemple :
  // if (touchRead(&data->point.x, &data->point.y))
  //   data->state = LV_INDEV_STATE_PRESSED;
  // else
  //   data->state = LV_INDEV_STATE_RELEASED;
  data->state = LV_INDEV_STATE_RELEASED; // placeholder
}

// ── Initialisation principale ────────────────────────────────────
void display_init() {
  lv_init();

  // TODO : init hardware Waveshare
  // tft.begin();  touch.begin();

  lv_disp_draw_buf_init(&draw_buf, buf, NULL, SCREEN_W * SCREEN_H / 10);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = SCREEN_W;
  disp_drv.ver_res  = SCREEN_H;
  disp_drv.flush_cb = disp_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  lv_indev_drv_register(&indev_drv);
}
