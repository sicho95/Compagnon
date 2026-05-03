/**
 * display_init.cpp
 * Fix v4 :
 *  - buf1 LVGL alloué en PSRAM (ps_malloc) au lieu de RAM statique
 *  - bus/gfx créés dans display_init() et non globalement
 *    (les new globaux s'exécutent avant setup() et crashent)
 */
#include "display_init.h"
#include "pin_config.h"
#include <Wire.h>
#include <Arduino.h>

// Pointeurs initialisés dans display_init() uniquement
Arduino_DataBus *bus = nullptr;
Arduino_CO5300  *gfx = nullptr;

#define BUF_PIXELS (SCREEN_W * SCREEN_H / 10)

static lv_color_t   *buf1  = nullptr;
static lv_display_t *disp  = nullptr;
static lv_indev_t   *indev = nullptr;

// ── Flush LVGL → écran ───────────────────────────────────────────────
static void disp_flush_cb(lv_display_t *display,
                           const lv_area_t *area,
                           uint8_t *px_map) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  lv_display_flush_ready(display);
}

// ── Tick LVGL ──────────────────────────────────────────────────────
static uint32_t lv_tick_get_cb_impl() { return (uint32_t)millis(); }

// ── Lecture touch CST816 ─────────────────────────────────────────────
static void touch_read_cb(lv_indev_t *dev, lv_indev_data_t *data) {
  data->state = LV_INDEV_STATE_RELEASED;

  Wire.beginTransmission(0x15);
  Wire.write(0x01);
  if (Wire.endTransmission(false) != 0) return;
  Wire.requestFrom((uint8_t)0x15, (uint8_t)6);
  if (Wire.available() < 6) return;

  Wire.read();               // GestureID
  uint8_t points = Wire.read();
  uint8_t xh     = Wire.read();
  uint8_t xl     = Wire.read();
  uint8_t yh     = Wire.read();
  uint8_t yl     = Wire.read();

  if ((points & 0x0F) > 0) {
    int16_t x = (int16_t)(((xh & 0x0F) << 8) | xl);
    int16_t y = (int16_t)(((yh & 0x0F) << 8) | yl);
    x = constrain(x, 0, SCREEN_W - 1);
    y = constrain(y, 0, SCREEN_H - 1);
    data->point.x = x;
    data->point.y = y;
    data->state   = LV_INDEV_STATE_PRESSED;
  }
}

// ── Initialisation principale ───────────────────────────────────────────
void display_init() {
  Serial.println("[DISPLAY] Init...");

  // 1. Buffer LVGL en PSRAM (48KB liberes de la RAM interne)
  buf1 = (lv_color_t *)ps_malloc(BUF_PIXELS * sizeof(lv_color_t));
  if (!buf1) {
    // PSRAM indisponible : fallback RAM interne réduit (1/20 écran)
    Serial.println("[DISPLAY] WARN: PSRAM absent, buf reduit");
    static lv_color_t fallback_buf[SCREEN_W * SCREEN_H / 20];
    buf1 = fallback_buf;
  }

  // 2. Reset touch CST816
#ifdef TOUCH_RES
  pinMode(TOUCH_RES, OUTPUT);
  digitalWrite(TOUCH_RES, LOW);  delay(20);
  digitalWrite(TOUCH_RES, HIGH); delay(100);
#endif

  // 3. Reset LCD CO5300 (cold power-on après shutdown AXP2101)
  pinMode(LCD_RESET, OUTPUT);
  digitalWrite(LCD_RESET, LOW);  delay(20);
  digitalWrite(LCD_RESET, HIGH); delay(120);

  // 4. I2C
  Wire.begin(IIC_SDA, IIC_SCL);
  Wire.setClock(400000);

  // 5. Création bus/gfx ICI (pas en global) pour éviter le crash pré-setup
  bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK,
    LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

  gfx = new Arduino_CO5300(
    bus, LCD_RESET,
    0 /* rotation */,
    LCD_WIDTH, LCD_HEIGHT,
    0, 0, 0, 0);

  // 6. Démarrage écran
  if (!gfx->begin()) {
    Serial.println("[DISPLAY] ERREUR gfx->begin() !");
    while (1) delay(100);
  }
  bus->writeC8D8(0x36, 0xA0);
  gfx->fillScreen(0x0000);
  gfx->setBrightness(200);
  Serial.println("[DISPLAY] Ecran OK 480x480");

  // 7. LVGL v9
  lv_init();
  lv_tick_set_cb(lv_tick_get_cb_impl);

  disp = lv_display_create(SCREEN_W, SCREEN_H);
  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_buffers(disp, buf1, NULL,
                          BUF_PIXELS * sizeof(lv_color_t),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);
  lv_indev_set_display(indev, disp);

  Serial.println("[DISPLAY] LVGL v9 + touch OK");
}
