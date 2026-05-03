/**
 * display_init.cpp
 * Branche la lib Mylibrary (Waveshare) sur LVGL v9.
 *
 * ⚠️  La lib Waveshare "Mylibrary" doit être installée dans
 *     Documents/Arduino/libraries/
 *     (repo waveshareteam/ESP32-S3-Touch-AMOLED-2.16
 *      → examples/Arduino-v3.3.5/libraries/Mylibrary)
 *
 * LVGL v9 — différences clés vs v8 :
 *   v8 : lv_disp_draw_buf_t / lv_disp_drv_t / lv_indev_drv_t
 *   v9 : lv_display_t       / lv_display_t  / lv_indev_t
 */
#include "display_init.h"

// ── Waveshare driver ─────────────────────────────────────────────
// Décommente une fois la lib installée :
// #include <ESP32_S3_AMOLED.h>

// ── Tampon LVGL v9 (1/10 écran, format RGB565) ───────────────────
#define BUF_SIZE (SCREEN_W * SCREEN_H / 10)
static uint8_t buf1[BUF_SIZE * sizeof(lv_color_t)];

static lv_display_t *disp  = NULL;
static lv_indev_t   *indev = NULL;

// ── Callback flush LVGL v9 → écran ───────────────────────────────
static void disp_flush_cb(lv_display_t *display,
                           const lv_area_t *area,
                           uint8_t *px_map) {
  // TODO : appel driver Waveshare
  // int32_t w = area->x2 - area->x1 + 1;
  // int32_t h = area->y2 - area->y1 + 1;
  // tft.pushImageDMA(area->x1, area->y1, w, h, (uint16_t*)px_map);
  lv_display_flush_ready(display);
}

// ── Callback touch LVGL v9 ────────────────────────────────────────
static void touch_read_cb(lv_indev_t *dev, lv_indev_data_t *data) {
  // TODO : lire le touch via Mylibrary
  // Exemple :
  // int16_t tx, ty;
  // if (touchGetXY(&tx, &ty)) {
  //   data->point.x = tx;
  //   data->point.y = ty;
  //   data->state   = LV_INDEV_STATE_PRESSED;
  // } else {
  //   data->state   = LV_INDEV_STATE_RELEASED;
  // }
  data->state = LV_INDEV_STATE_RELEASED; // placeholder
}

// ── Initialisation principale ─────────────────────────────────────
void display_init() {
  lv_init();

  // TODO : init hardware Waveshare
  // tft.begin();
  // touch.begin();

  // Création display LVGL v9
  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Création indev touch LVGL v9
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);
}
