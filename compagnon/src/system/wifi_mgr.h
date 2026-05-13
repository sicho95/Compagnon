#pragma once
#include <Arduino.h>

void   wifi_mgr_init();
void   wifi_mgr_tick();
bool   wifi_mgr_connected();

// Ajouter ou mettre à jour un réseau (max 10)
void   wifi_mgr_add_network(const char* ssid, const char* pwd);

// Supprimer un réseau sauvegardé
void   wifi_mgr_remove_network(const char* ssid);

// Retourne JSON des réseaux sauvegardés : [{"ssid":"..."},...]
String wifi_mgr_list_networks();

// Forcer un rescan + reconnexion (ex: après ajout réseau)
void   wifi_mgr_reconnect();

// Compat legacy : add_network + reconnect
void   wifi_mgr_provision(const char* ssid, const char* pwd);

// Scan asynchrone pour la PWA (BLE)
void   wifi_mgr_scan(void (*on_result)(const char* json));
