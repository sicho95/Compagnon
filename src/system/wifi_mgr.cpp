#include "wifi_mgr.h"
#include "../ui/status_bar.h"
#include <WiFi.h>
#include <Preferences.h>

// ── Stockage multi-réseau ─────────────────────────────────────────────────────
#define MAX_NETWORKS 10
struct WifiNet { char ssid[64]; char pwd[64]; };
static WifiNet   _nets[MAX_NETWORKS];
static int       _net_count = 0;

// ── Machine d'état ────────────────────────────────────────────────────────────
enum WifiState { IDLE, SCANNING_BEST, CONNECTING, CONNECTED, RETRY };
static WifiState     _state          = IDLE;
static unsigned long _state_ms       = 0;
static uint32_t      _retry_delay_ms = 2000;
static int           _retry_count    = 0;
static const int     MAX_RETRIES     = 5;
static int           _current_net    = -1;

// ── Scan BLE asynchrone ───────────────────────────────────────────────────────
static void (*_scan_cb)(const char* json) = nullptr;
static bool _ext_scanning = false;

// ── NVS ───────────────────────────────────────────────────────────────────────
static void _save_nvs() {
    Preferences prefs;
    prefs.begin("wifi_multi", false);
    prefs.putInt("count", _net_count);
    for (int i = 0; i < _net_count; i++) {
        char sk[4], pk[4];
        snprintf(sk, sizeof(sk), "s%d", i);
        snprintf(pk, sizeof(pk), "p%d", i);
        prefs.putString(sk, _nets[i].ssid);
        prefs.putString(pk, _nets[i].pwd);
    }
    prefs.end();
}

static void _load_nvs() {
    Preferences prefs;
    prefs.begin("wifi_multi", true);
    _net_count = prefs.getInt("count", 0);
    if (_net_count > MAX_NETWORKS) _net_count = MAX_NETWORKS;
    for (int i = 0; i < _net_count; i++) {
        char sk[4], pk[4];
        snprintf(sk, sizeof(sk), "s%d", i);
        snprintf(pk, sizeof(pk), "p%d", i);
        strlcpy(_nets[i].ssid, prefs.getString(sk, "").c_str(), 64);
        strlcpy(_nets[i].pwd,  prefs.getString(pk, "").c_str(), 64);
    }
    prefs.end();
    Serial.printf("[WIFI] %d réseau(x) chargés depuis NVS\n", _net_count);
}

// ── Trouver le meilleur réseau connu visible ──────────────────────────────────
// Scan synchrone (~2s) — utilisé uniquement au boot et après déconnexion
static int _find_best_network() {
    int n = WiFi.scanNetworks();   // bloquant
    if (n <= 0) { WiFi.scanDelete(); return -1; }

    int best_idx  = -1;
    int best_rssi = -9999;
    for (int s = 0; s < n; s++) {
        String sid = WiFi.SSID(s);
        int    rssi = WiFi.RSSI(s);
        for (int k = 0; k < _net_count; k++) {
            if (sid == _nets[k].ssid && rssi > best_rssi) {
                best_rssi = rssi;
                best_idx  = k;
            }
        }
    }
    WiFi.scanDelete();
    if (best_idx >= 0)
        Serial.printf("[WIFI] Meilleur: %s (%d dBm)\n", _nets[best_idx].ssid, best_rssi);
    return best_idx;
}

static void _begin_connect(int net_idx) {
    if (net_idx < 0 || net_idx >= _net_count) { _state = IDLE; return; }
    _current_net = net_idx;
    Serial.printf("[WIFI] Connexion à %s...\n", _nets[net_idx].ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_nets[net_idx].ssid, _nets[net_idx].pwd);
    _state    = CONNECTING;
    _state_ms = millis();
}

// ── API publique ──────────────────────────────────────────────────────────────
void wifi_mgr_add_network(const char* ssid, const char* pwd) {
    for (int i = 0; i < _net_count; i++) {
        if (strcmp(_nets[i].ssid, ssid) == 0) {
            strlcpy(_nets[i].pwd, pwd, 64);
            _save_nvs();
            Serial.printf("[WIFI] Réseau mis à jour: %s\n", ssid);
            return;
        }
    }
    if (_net_count >= MAX_NETWORKS) {
        Serial.println("[WIFI] Max réseaux atteint (10)");
        return;
    }
    strlcpy(_nets[_net_count].ssid, ssid, 64);
    strlcpy(_nets[_net_count].pwd,  pwd,  64);
    _net_count++;
    _save_nvs();
    Serial.printf("[WIFI] Réseau ajouté: %s (%d total)\n", ssid, _net_count);
}

void wifi_mgr_remove_network(const char* ssid) {
    for (int i = 0; i < _net_count; i++) {
        if (strcmp(_nets[i].ssid, ssid) == 0) {
            for (int j = i; j < _net_count - 1; j++) _nets[j] = _nets[j+1];
            _net_count--;
            _save_nvs();
            Serial.printf("[WIFI] Réseau supprimé: %s\n", ssid);
            return;
        }
    }
}

String wifi_mgr_list_networks() {
    String json = "[";
    for (int i = 0; i < _net_count; i++) {
        if (i > 0) json += ",";
        String s = _nets[i].ssid;
        s.replace("\\", "\\\\"); s.replace("\"", "\\\"");
        json += "{\"ssid\":\"" + s + "\"}";
    }
    json += "]";
    return json;
}

void wifi_mgr_provision(const char* ssid, const char* pwd) {
    wifi_mgr_add_network(ssid, pwd);
    wifi_mgr_reconnect();
}

void wifi_mgr_reconnect() {
    WiFi.disconnect(true);
    _state          = SCANNING_BEST;
    _state_ms       = millis();
    _retry_count    = 0;
    _retry_delay_ms = 2000;
    Serial.println("[WIFI] Rescan demandé");
}

void wifi_mgr_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    _load_nvs();
    if (_net_count > 0) {
        _state    = SCANNING_BEST;
        _state_ms = millis();
    }
}

void wifi_mgr_tick() {
    // ── Scan BLE asynchrone ────────────────────────────────────────────────
    if (_ext_scanning && _scan_cb) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            _ext_scanning = false;
            String json = "[";
            for (int i = 0; i < n; i++) {
                if (i > 0) json += ",";
                String sid = WiFi.SSID(i);
                sid.replace("\\", "\\\\"); sid.replace("\"", "\\\"");
                json += "{\"ssid\":\"" + sid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
            }
            json += "]";
            WiFi.scanDelete();
            _scan_cb(json.c_str());
            _scan_cb = nullptr;
        }
        return;
    }

    // ── Machine d'état WiFi ────────────────────────────────────────────────
    switch (_state) {
        case IDLE:
            break;

        case SCANNING_BEST: {
            int idx = _find_best_network();
            if (idx >= 0) {
                _begin_connect(idx);
            } else {
                Serial.println("[WIFI] Aucun réseau connu visible");
                _state          = RETRY;
                _state_ms       = millis();
                _retry_delay_ms = min(_retry_delay_ms * 2, (uint32_t)30000);
            }
            break;
        }

        case CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _state          = CONNECTED;
                _retry_count    = 0;
                _retry_delay_ms = 2000;
                Serial.println("[WIFI] Connecté : " + WiFi.localIP().toString());
                ui_status_bar_set_wifi(true);
            } else if (millis() - _state_ms > 12000) {
                WiFi.disconnect(true);
                _retry_count++;
                Serial.printf("[WIFI] Timeout, retry %d/%d\n", _retry_count, MAX_RETRIES);
                if (_retry_count >= MAX_RETRIES) {
                    Serial.println("[WIFI] Trop d'échecs, rescan autre réseau");
                    _retry_count    = 0;
                    _retry_delay_ms = 2000;
                    _state          = SCANNING_BEST;
                } else {
                    _state    = RETRY;
                    _state_ms = millis();
                    _retry_delay_ms = min(_retry_delay_ms * 2, (uint32_t)30000);
                }
            }
            break;

        case CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WIFI] Déconnecté, rescan...");
                ui_status_bar_set_wifi(false);
                _retry_count    = 0;
                _retry_delay_ms = 4000;
                _state          = RETRY;
                _state_ms       = millis();
            }
            break;

        case RETRY:
            if (millis() - _state_ms >= _retry_delay_ms) {
                _state    = SCANNING_BEST;
                _state_ms = millis();
            }
            break;
    }
}

bool wifi_mgr_connected() {
    return _state == CONNECTED && WiFi.status() == WL_CONNECTED;
}

void wifi_mgr_scan(void (*on_result)(const char* json)) {
    _scan_cb      = on_result;
    _ext_scanning = true;
    WiFi.scanNetworks(true);
}
