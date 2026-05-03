/**
 * display_init.h
 * Initialisation écran Waveshare ESP32-S3 Touch AMOLED 2.16"
 * Pilote   : SH8601 / CO5300  —  Interface : QSPI
 * Touch    : I2C (CST816 ou similaire)
 * Résolution : 480 × 480 px
 */
#pragma once
#include <lvgl.h>

#define SCREEN_W  480
#define SCREEN_H  480

/**
 * Initialise le matériel (QSPI, touch I2C) et enregistre
 * les drivers display + input auprès de LVGL.
 * À appeler une seule fois dans setup().
 */
void display_init();
