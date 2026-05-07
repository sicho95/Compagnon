#include "touch.h"
#include "display.h"            // hal_display_get() pour lv_indev_set_display()
#include "../config/pin_config.h"
#include <Wire.h>
#include <lvgl.h>

// ── CST9220 registres (adresse 7 bits 0x1A) ──────────────────────────
// 0x02 : nombre de points de contact (bits [3:0])
// 0x03 : X[11:8] dans bits [3:0]
// 0x04 : X[7:0]
// 0x05 : Y[11:8] dans bits [3:0]
// 0x06 : Y[7:0]
#define CST9220_ADDR    0x1A
#define CST9220_REG_NB  0x02

static bool touch_ok = false;

static void touch_read_cb(lv_indev_t *dev, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    if (!touch_ok) return;

    // Lecture de 5 registres contigus à partir de 0x02
    Wire.beginTransmission(CST9220_ADDR);
    Wire.write(CST9220_REG_NB);
    if (Wire.endTransmission(false) != 0) return;
    if (Wire.requestFrom((uint8_t)CST9220_ADDR, (uint8_t)5, (uint8_t)true) != 5) return;

    uint8_t nb   = Wire.read() & 0x0F;    // reg 0x02 : nombre de contacts
    uint8_t x_hi = Wire.read() & 0x0F;    // reg 0x03 : X[11:8]
    uint8_t x_lo = Wire.read();            // reg 0x04 : X[7:0]
    uint8_t y_hi = Wire.read() & 0x0F;    // reg 0x05 : Y[11:8]
    uint8_t y_lo = Wire.read();            // reg 0x06 : Y[7:0]

    if (nb == 0) return;

    int16_t x = (int16_t)(((uint16_t)x_hi << 8) | x_lo);
    int16_t y = (int16_t)(((uint16_t)y_hi << 8) | y_lo);

    // Contraindre à la résolution physique du panel
    x = constrain(x, 0, LCD_WIDTH  - 1);
    y = constrain(y, 0, LCD_HEIGHT - 1);

    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    data->state   = LV_INDEV_STATE_PRESSED;
}

void hal_touch_init() {
    // Restaurer la config I2C (peut avoir été modifiée par hal_pmu_init)
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    // Le CST9220 partage le reset (pin 2) avec l'écran : attendre le boot complet
    Serial.println("[HAL/TOUCH] Attente boot CST9220 (polling pur)...");
    delay(500);

    // Vérifier la présence du CST9220 sur le bus I2C
    Wire.beginTransmission(CST9220_ADDR);
    touch_ok = (Wire.endTransmission() == 0);

    if (!touch_ok) {
        Serial.printf("[HAL/TOUCH] CST9220 (0x%02X) non detecte — touch desactive\n",
                      CST9220_ADDR);
    } else {
        Serial.printf("[HAL/TOUCH] CST9220 detecte (0x%02X) — mode polling I2C pur\n",
                      CST9220_ADDR);
    }

    // Créer l'indev LVGL et l'associer au display pour que LVGL gère la
    // compensation de rotation (LV_DISPLAY_ROTATION_270 → inverse automatique)
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, hal_display_get());
}
