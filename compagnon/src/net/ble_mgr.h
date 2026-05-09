#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// BLE Manager V3 — service GATT multi-caractéristiques
//
// UUID Service      : 12345678-1234-5678-1234-56789abcdef0
// Char WIFI_SCAN    : 12345678-0001-5678-1234-56789abcdef0  (write+notify)
// Char WIFI_PROV    : 12345678-0002-5678-1234-56789abcdef0  (write)
// Char AGENT_SYNC   : 12345678-0003-5678-1234-56789abcdef0  (write+notify)
// Char TEXT_INPUT   : 12345678-0004-5678-1234-56789abcdef0  (write)
// Char LLM_RELAY    : 12345678-0005-5678-1234-56789abcdef0  (notify)
// Char DEVICE_STATUS: 12345678-0006-5678-1234-56789abcdef0  (notify)
// Char GPS          : 6e400003-b5a3-f393-e0a9-e50e24dcca9e  (write, float32 LE x2)
// ─────────────────────────────────────────────────────────────────────────────
#include <stdbool.h>

typedef void (*music_cmd_cb_t)(const char *cmd);
typedef void (*text_input_cb_t)(const char *text);
typedef void (*agent_sync_cb_t)(const char *json);
typedef void (*wifi_prov_cb_t)(const char *json);

void ble_mgr_init();
void ble_mgr_tick();
bool ble_mgr_connected();
bool ble_mgr_is_active();

// GPS — position reçue depuis la PWA (float32 LE, 8 octets)
bool ble_mgr_get_gps(double *lat, double *lon);

// Notifications vers la PWA
void ble_mgr_notify_llm(const char *json);
void ble_mgr_notify_device_status(const char *json);
void ble_mgr_notify_agent_sync(const char *json);
void ble_mgr_notify_wifi_scan(const char *json);

// Callbacks depuis la PWA
void ble_mgr_set_text_input_cb(text_input_cb_t cb);
void ble_mgr_set_agent_sync_cb(agent_sync_cb_t cb);
void ble_mgr_set_wifi_prov_cb(wifi_prov_cb_t cb);

// Compat music (radar/musique app)
void ble_mgr_set_music_cb(music_cmd_cb_t cb);
void ble_mgr_music_notify(const char *json);
