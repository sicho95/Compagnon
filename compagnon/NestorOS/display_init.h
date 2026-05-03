/**
 * display_init.h
 * Initialisation écran Waveshare ESP32-S3 Touch AMOLED 2.16"
 * Driver  : Arduino_CO5300 (via GFX_Library_for_Arduino)
 * Bus     : QSPI (Arduino_ESP32QSPI)
 * Touch   : I2C sur SDA=15 / SCL=14
 * Résolution réelle : 466 × 466 px
 *
 * Libs requises (Documents/Arduino/libraries/) :
 *   - GFX_Library_for_Arduino  (depuis repo Waveshare)
 *   - lvgl                     (depuis repo Waveshare)
 *   - lv_conf.h                (racine de libraries/)
 */
#pragma once
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include "pin_config.h"

// Résolution réelle de la carte (pin_config.h)
#define SCREEN_W  LCD_WIDTH    // 466
#define SCREEN_H  LCD_HEIGHT   // 466

/** Objet GFX global — accessible depuis les autres modules si besoin */
extern Arduino_CO5300 *gfx;

/**
 * Initialise le bus QSPI, l'écran AMOLED, le touch I2C
 * et enregistre les drivers display + indev auprès de LVGL v9.
 * À appeler une seule fois dans setup().
 */
void display_init();
