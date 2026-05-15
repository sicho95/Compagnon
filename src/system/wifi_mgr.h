#pragma once
#include <Arduino.h>

// ── WiFi Manager multi-réseau ─────────────────────────────────────────────────
// - Stocke jusqu'à 10 réseaux en NVS (Preferences)
// - Au boot/reconnexion : scan WiFi synchrone, connexion au réseau connu
//   ayant le meilleur RSSI
// - Retry exponentiel (2s → 4s → 8s → … → 30s max)

void wifi_mgr_init();
void wifi_mgr_tick();      // appeler dans loop()
bool wifi_mgr_connected();

// Ajouter/mettre à jour un réseau (crée ou écrase si même SSID)
void wifi_mgr_add_network(const char* ssid, const char* pwd);

// Supprimer un réseau sauvegardé
void wifi_mgr_remove_network(const char* ssid);

// Retourne JSON des SSIDs sauvegardés : [{"ssid":"..."},...]
String wifi_mgr_list_networks();

// Forcer un rescan + reconnexion (ex: après ajout réseau)
void wifi_mgr_reconnect();

// Compatibilité legacy BLE provision
void wifi_mgr_provision(const char* ssid, const char* pwd);

// Scan asynchrone pour l'UI BLE (résultat JSON via callback)
void wifi_mgr_scan(void (*on_result)(const char* json));
