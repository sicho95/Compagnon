/**
 * display_init.cpp
 * Driver écran : Arduino_CO5300 sur bus QSPI ESP32S3
 * Touch        : CST816 sur I2C (SDA=15, SCL=14)
 * LVGL         : v9  →  lv_display_t / lv_indev_t
 */
#include "display_init.h"
#include <Wire.h>

// ── Objet GFX global ───────────────────────────────────────────────────────
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK,
  LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *gfx = new Arduino_CO5300(
  bus, LCD_RESET,
  0 /* rotation */,
  LCD_WIDTH, LCD_HEIGHT,
  0, 0, 0, 0);

// ── Tampon LVGL v9 (1/10 écran) ───────────────────────────────────────────
#define BUF_SIZE (SCREEN_W * SCREEN_H / 10)
static uint8_t buf1[BUF_SIZE * sizeof(lv_color_t)];

static lv_display_t *disp  = NULL;
static lv_indev_t   *indev = NULL;

// ── Flush LVGL → écran (via GFX) ──────────────────────────────────────────
static void disp_flush_cb(lv_display_t *display,
                           const lv_area_t *area,
                           uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(display);
}

// ── Lecture touch CST816 (I2C) ─────────────────────────────────────────────
static void touch_read_cb(lv_indev_t *dev, lv_indev_data_t *data) {
  // Protocole CST816 : lecture 6 octets depuis l'adresse 0x15
  Wire.beginTransmission(0x15);
  Wire.write(0x02); // registre données touch
  Wire.endTransmission(false);
  Wire.requestFrom(0x15, 6);

  if (Wire.available() >= 6) {
    uint8_t gesture = Wire.read();
    uint8_t points  = Wire.read();
    uint8_t xh      = Wire.read();
    uint8_t xl      = Wire.read();
    uint8_t yh      = Wire.read();
    uint8_t yl      = Wire.read();

    if (points > 0) {
      data->point.x = ((xh & 0x0F) << 8) | xl;
      data->point.y = ((yh & 0x0F) << 8) | yl;
      data->state   = LV_INDEV_STATE_PRESSED;
    } else {
      data->state = LV_INDEV_STATE_RELEASED;
    }
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ── Initialisation principale ──────────────────────────────────────────────
void display_init() {
  // I2C pour touch
  Wire.begin(IIC_SDA, IIC_SCL);

  // Init écran AMOLED via GFX
  if (!gfx->begin()) {
    Serial.println("[DISPLAY] Erreur init écran !");
    while (1) delay(100); // bloque si pas d'écran
  }
  bus->writeC8D8(0x36, 0xA0); // orientation Waveshare
  gfx->fillScreen(BLACK);     // efface l'écran
  gfx->setBrightness(200);    // luminosité initiale (0-255)
  Serial.println("[DISPLAY] Écran OK 466x466");

  // LVGL v9
  lv_init();

  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);

  Serial.println("[DISPLAY] LVGL v9 initialisé");
}
