/**
 * display_init.h
 * Initialisation écran Waveshare ESP32-S3 Touch AMOLED 2.16"
 * Pilote   : SH8601 / CO5300  —  Interface : QSPI
 * Touch    : I2C (CST816 ou similaire)
 * Résolution : 480 × 480 px
 *
 * ⚠️  API LVGL v9  (lv_display_t / lv_indev_t)
 */
#pragma once
#include <lvgl.h>

#define SCREEN_W  480
#define SCREEN_H  480

void display_init();
