#include "wifi_mgr.h"
#include "../ui/status_bar.h"
#include <WiFi.h>
#include <Preferences.h>

// ─── Stockage multi-réseau (max 10) ──────────────────────────────────────────
#define MAX_NETWORKS 10
struct WifiNet { char ssid[64]; char pwd[64]; };
static WifiNet _nets[MAX_NETWORKS];
static int     _net_count = 0;

// ─── Machine d'état ──────────────────────────────────────────────────────────
enum WifiState { IDLE, SCANNING_BEST, CONNECTING, CONNECTED, RETRY };
static WifiState     _state          = IDLE;
static unsigned long _state_ms       = 0;
static uint32_t      _retry_delay_ms = 2000;
static int           _retry_count    = 0;
static const int     MAX_RETRIES     = 5;
static int           _current_net    = -1;

// Scan externe (BLE)
static void (*_scan_cb)(const char* json) = nullptr;
static bool  _ext_scanning = false;

// ─── NVS ─────────────────────────────────────────────────────────────────────
static void save_nvs() {
    Preferences prefs;
    prefs.begin("wifi_multi", false);
    prefs.putInt("count", _net_count);
    for (int i = 0; i < _net_count; i++) {
        prefs.putString(("s" + String(i)).c_str(), _nets[i].ssid);
        prefs.putString(("p" + String(i)).c_str(), _nets[i].pwd);
    }
    prefs.end();
}

static void load_nvs() {
    Preferences prefs;
    prefs.begin("wifi_multi", true);
    _net_count = prefs.getInt("count", 0);
    if (_net_count > MAX_NETWORKS) _net_count = MAX_NETWORKS;
    for (int i = 0; i < _net_count; i++) {
        strlcpy(_nets[i].ssid, prefs.getString(("s" + String(i)).c_str(), "").c_str(), 64);
        strlcpy(_nets[i].pwd,  prefs.getString(("p" + String(i)).c_str(), "").c_str(), 64);
    }
    prefs.end();
    // Compatibilité : migration depuis l'ancien namespace "wifi" (ssid/pwd unique)
    if (_net_count == 0) {
        Preferences old;
        old.begin("wifi", true);
        String old_ssid = old.getString("ssid", "");
        String old_pwd  = old.getString("pwd",  "");
        old.end();
        if (old_ssid.length() > 0) {
            strlcpy(_nets[0].ssid, old_ssid.c_str(), 64);
            strlcpy(_nets[0].pwd,  old_pwd.c_str(),  64);
            _net_count = 1;
            save_nvs();
            Serial.println("[WIFI] Migration ancien profil unique -> multi");
        }
    }
    Serial.printf("[WIFI] %d réseau(x) chargés depuis NVS\n", _net_count);
}

// ─── Trouver le réseau connu avec le meilleur RSSI visible ──────────────────
static int find_best_network() {
    Serial.println("[WIFI] Scan des réseaux disponibles...");
    int n = WiFi.scanNetworks();  // scan synchrone
    if (n <= 0) {
        Serial.println("[WIFI] Aucun réseau détecté");
        return -1;
    }
    int best_idx  = -1;
    int best_rssi = -9999;
    for (int s = 0; s < n; s++) {
        String scanned = WiFi.SSID(s);
        int    rssi    = WiFi.RSSI(s);
        for (int k = 0; k < _net_count; k++) {
            if (scanned == _nets[k].ssid && rssi > best_rssi) {
                best_rssi = rssi;
                best_idx  = k;
            }
        }
    }
    WiFi.scanDelete();
    if (best_idx >= 0)
        Serial.printf("[WIFI] Meilleur réseau connu: %s (%d dBm)\n",
                      _nets[best_idx].ssid, best_rssi);
    else
        Serial.println("[WIFI] Aucun réseau connu visible");
    return best_idx;
}

static void begin_connect(int net_idx) {
    if (net_idx < 0 || net_idx >= _net_count) { _state = IDLE; return; }
    _current_net = net_idx;
    Serial.printf("[WIFI] Connexion à %s...\n", _nets[net_idx].ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_nets[net_idx].ssid, _nets[net_idx].pwd);
    _state    = CONNECTING;
    _state_ms = millis();
}

// ─── API publique ─────────────────────────────────────────────────────────────
void wifi_mgr_add_network(const char* ssid, const char* pwd) {
    if (!ssid || ssid[0] == '\0') return;
    // Mise à jour si le SSID existe déjà
    for (int i = 0; i < _net_count; i++) {
        if (strcmp(_nets[i].ssid, ssid) == 0) {
            strlcpy(_nets[i].pwd, pwd ? pwd : "", 64);
            save_nvs();
            Serial.printf("[WIFI] Réseau mis à jour: %s\n", ssid);
            return;
        }
    }
    if (_net_count >= MAX_NETWORKS) {
        Serial.println("[WIFI] Liste pleine (10 max), réseau ignoré");
        return;
    }
    strlcpy(_nets[_net_count].ssid, ssid,        64);
    strlcpy(_nets[_net_count].pwd,  pwd ? pwd : "", 64);
    _net_count++;
    save_nvs();
    Serial.printf("[WIFI] Réseau ajouté: %s (%d total)\n", ssid, _net_count);
}

void wifi_mgr_remove_network(const char* ssid) {
    if (!ssid) return;
    for (int i = 0; i < _net_count; i++) {
        if (strcmp(_nets[i].ssid, ssid) == 0) {
            for (int j = i; j < _net_count - 1; j++) _nets[j] = _nets[j + 1];
            _net_count--;
            save_nvs();
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

// Compat legacy : add + reconnect
void wifi_mgr_provision(const char* ssid, const char* pwd) {
    wifi_mgr_add_network(ssid, pwd);
    wifi_mgr_reconnect();
}

void wifi_mgr_reconnect() {
    WiFi.disconnect(true);
    _retry_count    = 0;
    _retry_delay_ms = 2000;
    _state          = SCANNING_BEST;
    _state_ms       = millis();
}

void wifi_mgr_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    load_nvs();
    if (_net_count > 0) {
        _state    = SCANNING_BEST;
        _state_ms = millis();
    }
}

void wifi_mgr_tick() {
    // ── Scan externe asynchrone (pour BLE scan) ───────────────────────────────
    if (_ext_scanning && _scan_cb) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            _ext_scanning = false;

            // Construire le JSON avec les informations complètes :
            // {ssid, rssi, secured, channel}
            // secured = false uniquement pour WIFI_AUTH_OPEN, true dans tous les autres cas
            String json = "[";
            for (int i = 0; i < n; i++) {
                if (i > 0) json += ",";
                String s = WiFi.SSID(i);
                // Échapper les guillemets et backslashes dans le SSID
                s.replace("\\", "\\\\");
                s.replace("\"", "\\\"");
                bool secured = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
                int  ch      = WiFi.channel(i);
                json += "{\"ssid\":\"" + s + "\","
                      + "\"rssi\":"    + String(WiFi.RSSI(i)) + ","
                      + "\"secured\":" + (secured ? "true" : "false") + ","
                      + "\"channel\":" + String(ch) + "}";
            }
            json += "]";
            WiFi.scanDelete();
            Serial.printf("[WIFI] Scan BLE terminé: %d réseau(x)\n", n);
            _scan_cb(json.c_str());
            _scan_cb = nullptr;
        }
        return;
    }

    // ── Machine d'état principale ─────────────────────────────────────────────
    switch (_state) {
        case IDLE:
            break;

        case SCANNING_BEST: {
            int idx = find_best_network();
            if (idx >= 0) {
                begin_connect(idx);
            } else {
                // Aucun réseau connu visible → attendre et réessayer
                _state    = RETRY;
                _state_ms = millis();
            }
            break;
        }

        case CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _state          = CONNECTED;
                _retry_delay_ms = 2000;
                _retry_count    = 0;
                Serial.println("[WIFI] Connecté : " + WiFi.localIP().toString());
                ui_status_bar_set_wifi(true);
            } else if (millis() - _state_ms > 12000) {
                WiFi.disconnect(true);
                _retry_count++;
                if (_retry_count >= MAX_RETRIES) {
                    Serial.println("[WIFI] Échec répété, rescan pour autre réseau");
                    _retry_count    = 0;
                    _retry_delay_ms = 2000;
                    _state          = SCANNING_BEST;
                    _state_ms       = millis();
                } else {
                    Serial.printf("[WIFI] Timeout connexion, retry %d/%d\n",
                                  _retry_count, MAX_RETRIES);
                    _state    = RETRY;
                    _state_ms = millis();
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
                _retry_delay_ms = min(_retry_delay_ms * 2, (uint32_t)30000);
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
    WiFi.scanNetworks(true);  // asynchrone
}
