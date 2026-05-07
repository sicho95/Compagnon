#include "display.h"
#include "../config/pin_config.h"

Arduino_DataBus *gfx_bus = nullptr;
Arduino_CO5300  *gfx     = nullptr;

static lv_display_t *s_disp = nullptr;

#define BUF_LINES 40

static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

// Swap bytes inline pour Big Endian sans toucher lv_conf.h
static void swap16_buf(uint16_t *buf, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    buf[i] = (buf[i] >> 8) | (buf[i] << 8);
  }
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  swap16_buf((uint16_t *)px_map, w * h);   // LE → BE avant envoi
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t *e) {
  lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
  if (!a) return;
  if (a->x1 % 2)    a->x1--;
  if (a->y1 % 2)    a->y1--;
  if (!(a->x2 % 2)) a->x2++;
  if (!(a->y2 % 2)) a->y2++;
}

static uint32_t tick_cb() { return (uint32_t)millis(); }

void hal_display_init() {
  Serial.println("[HAL/DISP] Init...");

  pinMode(LCD_RESET, OUTPUT);
  digitalWrite(LCD_RESET, LOW);  delay(20);
  digitalWrite(LCD_RESET, HIGH); delay(120);

  gfx_bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK,
    LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3
  );

  // Rotation 0 : pas de rotation hardware — LVGL gère la rotation logicielle (270°)
  gfx = new Arduino_CO5300(gfx_bus, LCD_RESET, 0,
                            LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, 0);

  if (!gfx->begin()) {
    Serial.println("[HAL/DISP] ECHEC begin() - BLOQUE");
    while (1) delay(500);
  }

  gfx->displayOn();
  gfx->setBrightness(200);
  gfx->fillScreen(0x0000);
  Serial.println("[HAL/DISP] GFX OK");

  lv_init();
  lv_tick_set_cb(tick_cb);

  size_t bytes = LCD_WIDTH * BUF_LINES * sizeof(lv_color_t);
  buf1 = (lv_color_t *)ps_malloc(bytes);
  buf2 = (lv_color_t *)ps_malloc(bytes);
  if (!buf1) { buf1 = (lv_color_t *)malloc(bytes); buf2 = nullptr; }

  s_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
  lv_display_set_buffers(s_disp, buf1, buf2, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(s_disp, flush_cb);
  lv_display_add_event_cb(s_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
  // Rotation LVGL 270° (= 90° anti-horaire) — le CO5300 est en rotation 0 hardware
  lv_display_set_rotation(s_disp, LV_DISPLAY_ROTATION_270);
  Serial.println("[HAL/DISP] LVGL OK");
}

lv_display_t *hal_display_get() { return s_disp; }