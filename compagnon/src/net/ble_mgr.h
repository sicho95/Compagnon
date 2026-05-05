#pragma once
// ─────────────────────────────────────────────────────────────────────
// BLE Manager V1 — service GATT "GPS Position" (push depuis téléphone)
//
// UUID Service   : 4FAFC201-1FB5-459E-8FCC-C5C9C331914B
// UUID Charact.  : BEB5483E-36E1-4688-B7F5-EA07361B26A8
//   → writable + notify
//   → format  : "lat,lon"  ex "48.8566,2.3522"
//
// La PWA écrit cette caractéristique via Web Bluetooth après avoir
// récupéré la position via Geolocation API du navigateur.
// ─────────────────────────────────────────────────────────────────────
#include <stdbool.h>

void ble_mgr_init();
void ble_mgr_tick();
bool ble_mgr_connected();

// Dernière position reçue via BLE (retourne false si aucune position connue)
bool ble_mgr_get_gps(double *lat, double *lon);

// Statut pour la status bar
bool ble_mgr_is_active();
