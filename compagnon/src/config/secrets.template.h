/**
 * OBSOLÈTE — Ne plus utiliser secrets.h pour les clés API.
 *
 * Depuis la refonte BLE provisioning (mai 2026), toutes les clés API
 * arrivent EXCLUSIVEMENT via la PWA (menu Réglages → onglet par app)
 * et sont poussées automatiquement vers l'ESP32 via BLE.
 * Elles sont stockées en NVS chiffrée via nvs_config.h
 *
 * Seules les constantes réseau du portail captif restent ici :
 */

#ifndef __SECRETS_H__
#define __SECRETS_H__

// WiFi du portail captif (premier démarrage uniquement)
#define WIFI_AP_SSID  "Compagnon_Setup"
#define WIFI_AP_PSK   "compagnon"

// TOUTES LES CLÉS API sont désormais gérées via nvs_config.h
// Elles sont saisies dans la PWA (Réglages) et poussées via BLE.

#endif // __SECRETS_H__
