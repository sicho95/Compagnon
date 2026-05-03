/**
 * Initialisation écran Waveshare ESP32-S3 Touch AMOLED 2.16"
 * Pilote : SH8601 / CO5300  —  Interface : QSPI
 * Résolution : 480 x 480 px
 *
 * TODO : remplacer par le driver Waveshare officiel
 * https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16
 */
#pragma once
#include <lvgl.h>

#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 480

void display_init();
