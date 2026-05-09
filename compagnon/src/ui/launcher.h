#pragma once
#include <lvgl.h>
void ui_launcher_init();
void ui_launcher_return();
void ui_power_menu_show();    // Overlay modal : Veille / Arrêt complet
void ui_launcher_btn_tick();  // Polling boutons — appeler depuis loop() directement

// Ramener la bordure écran au premier plan (appeler après tout nouvel overlay)
void ui_frame_to_front();
