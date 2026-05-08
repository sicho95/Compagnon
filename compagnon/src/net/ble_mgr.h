#pragma once
// ─────────────────────────────────────────────────────────────────────
// BLE Manager — service GATT Nestor
//
// UUID Service        : 12345678-1234-5678-1234-56789ABCDEF0
//
// UUID GPS            : 6E400003-B5A3-F393-E0A9-E50E24DCCA9E   WRITE
//   → 8 octets float32 little-endian : latitude (4) + longitude (4)
//   → La PWA envoie la position via watchPosition() toutes les ≤1 s
//
// UUID WIFI_SCAN      : 12345678-0001-5678-1234-56789ABCDEF0   WRITE + NOTIFY
//   → écrire 0x01 pour déclencher WiFi.scanNetworks(async=true)
//   → notification JSON : [{"s":"SSID","r":-65},...]  (triés par RSSI, max 10)
//
// UUID WIFI_PROVISION : 12345678-0002-5678-1234-56789ABCDEF0   WRITE
//   → JSON : {"ssid":"MonRéseau","password":"monMotDePasse"}
//   → appelle WiFi.begin() avec les nouvelles credentials
// ─────────────────────────────────────────────────────────────────────
#include <stdbool.h>

void ble_mgr_init();
void ble_mgr_tick();
bool ble_mgr_connected();
bool ble_mgr_is_active();

// Dernière position GPS reçue (retourne false si aucun fix)
bool ble_mgr_get_gps(double *lat, double *lon);
