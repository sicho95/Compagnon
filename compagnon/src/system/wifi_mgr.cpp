#include "wifi_mgr.h"
#include "../ui/status_bar.h"
#include <WiFi.h>
#include <Preferences.h>

enum WifiState { IDLE, CONNECTING, CONNECTED, RETRY };

static WifiState     _state          = IDLE;
static unsigned long _state_ms       = 0;
static uint32_t      _retry_delay_ms = 2000;
static int           _retry_count    = 0;
static const int     MAX_RETRIES     = 5;

static char _ssid[64] = "";
static char _pwd[64]  = "";

static void (*_scan_cb)(const char* json) = nullptr;
static bool _scanning = false;

static void load_nvs() {
    Preferences prefs;
    prefs.begin("wifi", true);
    strlcpy(_ssid, prefs.getString("ssid", "").c_str(), sizeof(_ssid));
    strlcpy(_pwd,  prefs.getString("pwd",  "").c_str(), sizeof(_pwd));
    prefs.end();
}

static void save_nvs() {
    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("ssid", _ssid);
    prefs.putString("pwd",  _pwd);
    prefs.end();
}

static void begin_connect() {
    if (_ssid[0] == '\0') { _state = IDLE; return; }
    Serial.printf("[WIFI] Connexion à %s...\n", _ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _pwd);
    _state    = CONNECTING;
    _state_ms = millis();
}

void wifi_mgr_init() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    load_nvs();
    begin_connect();
}

void wifi_mgr_tick() {
    if (_scanning && _scan_cb) {
        int n = WiFi.scanComplete();
        if (n >= 0) {
            _scanning = false;
            String json = "[";
            for (int i = 0; i < n; i++) {
                if (i > 0) json += ",";
                json += "{\"ssid\":\"";
                json += WiFi.SSID(i);
                json += "\",\"rssi\":";
                json += WiFi.RSSI(i);
                json += "}";
            }
            json += "]";
            WiFi.scanDelete();
            _scan_cb(json.c_str());
            _scan_cb = nullptr;
        }
    }

    switch (_state) {
        case IDLE:
            break;

        case CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                _state          = CONNECTED;
                _retry_delay_ms = 2000;
                _retry_count    = 0;
                Serial.println("[WIFI] Connecté : " + WiFi.localIP().toString());
                ui_status_bar_set_wifi(true);
            } else if (millis() - _state_ms > 10000) {
                WiFi.disconnect(true);
                _state    = RETRY;
                _state_ms = millis();
            }
            break;

        case CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WIFI] Déconnecté");
                ui_status_bar_set_wifi(false);
                _retry_count    = 0;
                _retry_delay_ms = 2000;
                _state          = RETRY;
                _state_ms       = millis();
            }
            break;

        case RETRY:
            if (millis() - _state_ms >= _retry_delay_ms) {
                _retry_count++;
                if (_retry_count > MAX_RETRIES) {
                    Serial.println("[WIFI] Max retries atteint, IDLE");
                    _state       = IDLE;
                    _retry_count = 0;
                    break;
                }
                Serial.printf("[WIFI] Retry %d/%d (délai suivant %lums)\n",
                              _retry_count, MAX_RETRIES,
                              min(_retry_delay_ms * 2, (uint32_t)30000));
                uint32_t next_delay = min(_retry_delay_ms * 2, (uint32_t)30000);
                begin_connect();
                _retry_delay_ms = next_delay;
            }
            break;
    }
}

bool wifi_mgr_connected() {
    return _state == CONNECTED && WiFi.status() == WL_CONNECTED;
}

void wifi_mgr_provision(const char* ssid, const char* pwd) {
    strlcpy(_ssid, ssid, sizeof(_ssid));
    strlcpy(_pwd,  pwd,  sizeof(_pwd));
    save_nvs();
    WiFi.disconnect(true);
    _retry_count    = 0;
    _retry_delay_ms = 2000;
    begin_connect();
}

void wifi_mgr_scan(void (*on_result)(const char* json)) {
    _scan_cb  = on_result;
    _scanning = true;
    WiFi.scanNetworks(true);
}
