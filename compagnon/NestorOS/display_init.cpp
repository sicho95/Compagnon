/**
 * display_init.cpp
 * Driver écran : Arduino_CO5300 sur bus QSPI ESP32S3
 * Touch        : CST816 sur I2C (SDA=15, SCL=14)
 * LVGL         : v9  →  lv_display_t / lv_indev_t
 *
 * Fix v3 :
 *  - Reset hardware LCD (GPIO45) avant gfx->begin() pour cold power-on
 *  - lv_tick_set_cb() pour que LVGL ait un timer correct
 */
#include "display_init.h"
#include "pin_config.h"
#include <Wire.h>
#include <Arduino.h>

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
static lv_color_t buf1[BUF_SIZE];

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

// ── Tick LVGL via millis() ────────────────────────────────────────────────
static uint32_t lv_tick_get_cb_impl() {
  return (uint32_t)millis();
}

// ── Lecture touch CST816 ──────────────────────────────────────────────────
// Registres CST816S (addr 0x15) :
//  0x01 = GestureID, 0x02 = FingerNum, 0x03 XH, 0x04 XL, 0x05 YH, 0x06 YL
static void touch_read_cb(lv_indev_t *dev, lv_indev_data_t *data) {
  data->state = LV_INDEV_STATE_RELEASED;

  Wire.beginTransmission(0x15);
  Wire.write(0x01);           // commence à GestureID
  if (Wire.endTransmission(false) != 0) return;
  Wire.requestFrom((uint8_t)0x15, (uint8_t)6);
  if (Wire.available() < 6) return;

  /*uint8_t gesture =*/ Wire.read();  // 0x01 GestureID (ignoré)
  uint8_t points  = Wire.read();      // 0x02 FingerNum
  uint8_t xh      = Wire.read();      // 0x03
  uint8_t xl      = Wire.read();      // 0x04
  uint8_t yh      = Wire.read();      // 0x05
  uint8_t yl      = Wire.read();      // 0x06

  if ((points & 0x0F) > 0) {
    int16_t x = (int16_t)(((xh & 0x0F) << 8) | xl);
    int16_t y = (int16_t)(((yh & 0x0F) << 8) | yl);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= SCREEN_W) x = SCREEN_W - 1;
    if (y >= SCREEN_H) y = SCREEN_H - 1;
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
  }
}

// ── Initialisation principale ──────────────────────────────────────────────
void display_init() {
  // ── 1. Reset hardware CST816 (touch) AVANT Wire.begin ──────────────────
#ifdef TOUCH_RES
  pinMode(TOUCH_RES, OUTPUT);
  digitalWrite(TOUCH_RES, LOW);  delay(20);
  digitalWrite(TOUCH_RES, HIGH); delay(100);
#endif

  // ── 2. Reset hardware LCD (CO5300) pour cold power-on ──────────────────
  //    Arduino_CO5300 passe déjà par gfx->begin() mais un reset manuel
  //    garantit l'état correct après un shutdown AXP2101.
  pinMode(LCD_RESET, OUTPUT);
  digitalWrite(LCD_RESET, LOW);  delay(20);
  digitalWrite(LCD_RESET, HIGH); delay(120);

  // ── 3. I2C (partagé CST816 + AXP2101) ─────────────────────────────────
  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setClock(400000);

  // ── 4. Ecran ───────────────────────────────────────────────────────────
  if (!gfx->begin()) {
    Serial.println("[DISPLAY] Erreur init ecran !");
    while (1) delay(100);
  }
  bus->writeC8D8(0x36, 0xA0);
  gfx->fillScreen(0x0000);
  gfx->setBrightness(200);
  Serial.println("[DISPLAY] Ecran OK 480x480");

  // ── 5. LVGL v9 ────────────────────────────────────────────────────────
  lv_init();
  lv_tick_set_cb(lv_tick_get_cb_impl);

  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_buffers(disp, buf1, NULL, sizeof(buf1),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);
  lv_indev_set_display(indev, disp);

  Serial.println("[DISPLAY] LVGL v9 + touch OK");
}
