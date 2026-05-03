#pragma once
#include <lvgl.h>

void status_bar_init();   // Appeler 1x après display_init()
void status_bar_tick();   // Appeler dans loop() — rafraîchit heure/batt/icônes
