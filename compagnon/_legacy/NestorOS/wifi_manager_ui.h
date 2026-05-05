#pragma once
void wifi_manager_init();   // Appeler dans setup() après display_init()
void wifi_manager_tick();   // Appeler dans loop() si en mode portail
bool wifi_manager_connected(); // true si connecté avec credentials valides
