#include "display_init.h"

// Tampon LVGL — 1/10 de l'écran
static lv_color_t buf[SCREEN_WIDTH * SCREEN_HEIGHT / 10];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

// ── Callback flush LVGL → écran ─────────────────────────────────────────────
static void my_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *color_p) {
  // TODO : appel driver AMOLED (SPI/DMA)
  // Ex : tft.pushImageDMA(area->x1, area->y1, w, h, (uint16_t*)color_p);
  lv_disp_flush_ready(drv);
}

void display_init() {
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf, NULL,
                         SCREEN_WIDTH * SCREEN_HEIGHT / 10);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = SCREEN_WIDTH;
  disp_drv.ver_res  = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  // TODO : init touch (I2C CST816 ou similaire)
}
