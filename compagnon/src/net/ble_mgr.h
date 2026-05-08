#pragma once
// ─────────────────────────────────────────────────────────────────────
// BLE Manager V2 — service GATT multi-caractéristiques
//
// UUID Service   : 4FAFC201-1FB5-459E-8FCC-C5C9C331914B
// Char GPS       : BEB5483E-36E1-4688-B7F5-EA07361B26A8  (write)
// Char Music CMD : A0B1C2D3-0001-2222-3333-AABB12345678  (write)
// Char Music Stat: A0B1C2D3-0002-2222-3333-AABB12345678  (notify)
// ─────────────────────────────────────────────────────────────────────
#include <stdbool.h>

typedef void (*music_cmd_cb_t)(const char *cmd);

void ble_mgr_init();
void ble_mgr_tick();
bool ble_mgr_connected();

// GPS
bool ble_mgr_get_gps(double *lat, double *lon);

// Music — enregistrer le callback de commande et envoyer une notification
void ble_mgr_set_music_cb(music_cmd_cb_t cb);
void ble_mgr_music_notify(const char *json);

// Statut pour la status bar
bool ble_mgr_is_active();

// Dernière position GPS reçue (retourne false si aucun fix)
bool ble_mgr_get_gps(double *lat, double *lon);
