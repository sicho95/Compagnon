#pragma once
// Bibliothèques requises (Library Manager) :
//   pschatzmann/arduino-audio-tools  (AirPlay + décodeurs)
//   schreibfaul1/ESP32-audioI2S      (optionnel, lecture locale I2S)
void musique_app_start();
void musique_app_stop();
void musique_app_tick();
void musique_ble_cmd(const char *cmd); // Traiter une commande BLE reçue de la PWA
