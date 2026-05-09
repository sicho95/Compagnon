#include "touch.h"
#include "../config/pin_config.h"
#include <Wire.h>
#include <TouchDrvCSTXXX.hpp>
#include <lvgl.h>

static TouchDrvCSTXXX touch;
static bool           touch_ok = false;

static void touch_read_cb(lv_indev_t *dev, lv_indev_data_t *data) {
    data->state = LV_INDEV_STATE_RELEASED;
    if (!touch_ok) return;

    int16_t x = 0, y = 0;
    if (touch.getPoint(&x, &y, 1) > 0) {
        data->point.x = (lv_coord_t)constrain(x, 0, LCD_WIDTH  - 1);
        data->point.y = (lv_coord_t)constrain(y, 0, LCD_HEIGHT - 1);
        data->state   = LV_INDEV_STATE_PRESSED;
    }
}

void hal_touch_init() {
    // Réinitialiser Wire proprement après les inits PMU/display qui l'ont perturbé
    Wire.end();
    delay(20);
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    // CST9220 nécessite ~800 ms minimum après reset hardware (pin 2 partagée avec LCD)
    // hal_display_init() a attendu 120 ms → on complète ici
    Serial.println("[HAL/TOUCH] Attente boot CST9220 (800 ms)...");
    delay(680);

    if (TOUCH_INT >= 0) pinMode(TOUCH_INT, INPUT_PULLUP);
    // -1 pour RST et INT : active le mode polling I²C pur.
    // Si INT est passé à SensorLib, getPoint() vérifie digitalRead(INT) == LOW
    // avant chaque lecture — le CST9220 pulse INT ~5 ms, trop court pour être
    // capturé à chaque tick LVGL (~16 ms), ce qui bloque systématiquement la lecture.
    touch.setPins(-1, -1);

    // Essai des adresses 7 bits connues pour la famille CST : 0x1A, 0x15, 0x5A
    touch_ok = touch.begin(Wire, 0x1A, IIC_SDA, IIC_SCL);
    if (!touch_ok) {
        Serial.println("[HAL/TOUCH] 0x1A echoue, essai 0x15...");
        touch_ok = touch.begin(Wire, 0x15, IIC_SDA, IIC_SCL);
    }
    if (!touch_ok) {
        Serial.println("[HAL/TOUCH] 0x15 echoue, essai 0x5A...");
        Wire.end(); delay(50); Wire.begin(IIC_SDA, IIC_SCL); Wire.setClock(400000); delay(100);
        touch_ok = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    }

    if (!touch_ok) {
        Serial.println("[HAL/TOUCH] CST9220 non detecte — touch desactive");
    } else {
        Serial.printf("[HAL/TOUCH] %s OK\n", touch.getModelName());
        touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
        // Rotation=2 (180°) : miroir X et Y, pas de swap
        touch.setSwapXY(false);
        touch.setMirrorXY(true, true);
    }

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
}
