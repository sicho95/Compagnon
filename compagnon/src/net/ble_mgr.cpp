#include "ble_mgr.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <stdlib.h>

// ─── UUIDs alignés sur la PWA (src/bt/ble.js) ────────────────────────────────
#define SERVICE_UUID       "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_WIFI_SCAN     "12345678-0001-5678-1234-56789abcdef0"
#define CHAR_WIFI_PROV     "12345678-0002-5678-1234-56789abcdef0"
#define CHAR_AGENT_SYNC    "12345678-0003-5678-1234-56789abcdef0"
#define CHAR_TEXT_INPUT    "12345678-0004-5678-1234-56789abcdef0"
#define CHAR_LLM_RELAY     "12345678-0005-5678-1234-56789abcdef0"
#define CHAR_DEVICE_STATUS "12345678-0006-5678-1234-56789abcdef0"
#define CHAR_GPS           "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

#define DEVICE_NAME  "Nestor"
#define MTU_MAX      512
// 7 caract. × 3 handles (déclaration + valeur + CCCD) + 1 service = 22 minimum
// On prend 30 pour avoir une marge confortable
#define BLE_HANDLE_COUNT 30

// ─── Handles ──────────────────────────────────────────────────────────────────
static BLEServer         *_server          = nullptr;
static BLECharacteristic *_c_wifi_scan     = nullptr;
static BLECharacteristic *_c_wifi_prov     = nullptr;
static BLECharacteristic *_c_agent_sync    = nullptr;
static BLECharacteristic *_c_text_input    = nullptr;
static BLECharacteristic *_c_llm_relay     = nullptr;
static BLECharacteristic *_c_device_status = nullptr;
static BLECharacteristic *_c_gps           = nullptr;

static bool   _connected  = false;
static bool   _active     = false;
static bool   _scanning   = false;
static double _lat        = 0.0;
static double _lon        = 0.0;
static bool   _has_gps    = false;

static music_cmd_cb_t   _music_cb      = nullptr;
static text_input_cb_t  _text_input_cb = nullptr;
static agent_sync_cb_t  _agent_sync_cb = nullptr;
static wifi_prov_cb_t   _wifi_prov_cb  = nullptr;

// ─── Connexion ────────────────────────────────────────────────────────────────
class ConnCB : public BLEServerCallbacks {
    void onConnect(BLEServer *) override {
        _connected = true;
        Serial.println("[BLE] Telephone connecte");
    }
    void onDisconnect(BLEServer *s) override {
        _connected = false;
        _has_gps   = false;
        Serial.println("[BLE] Deconnecte — re-advertising");
        s->startAdvertising();
    }
};

// ─── GPS : 8 octets float32 little-endian (lat 0-3, lon 4-7) ─────────────────
class GpsCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if ((int)val.length() < 8) return;
        const uint8_t *b = (const uint8_t *)val.c_str();
        float lat, lon;
        memcpy(&lat, b,     4);
        memcpy(&lon, b + 4, 4);
        if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) return;
        _lat = (double)lat;
        _lon = (double)lon;
        _has_gps = true;
        Serial.printf("[BLE] GPS: %.5f, %.5f\n", _lat, _lon);
    }
};

// ─── WiFi scan : écriture 0x01 démarre un scan asynchrone ────────────────────
class WifiScanCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0) return;
        if ((uint8_t)val[0] == 0x01 && !_scanning) {
            _scanning = true;
            WiFi.scanNetworks(true);
            Serial.println("[BLE] WiFi scan lance");
        }
    }
};

// ─── WiFi provisioning : JSON {"s":"ssid","p":"pass"} ─────────────────────────
class WifiProvCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0 || !_wifi_prov_cb) return;
        Serial.printf("[BLE] WiFi prov: %s\n", val.c_str());
        _wifi_prov_cb(val.c_str());
    }
};

// ─── Agent sync : JSON envoyé par la PWA ──────────────────────────────────────
class AgentSyncCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0 || !_agent_sync_cb) return;
        Serial.printf("[BLE] AgentSync: %u bytes\n", val.length());
        _agent_sync_cb(val.c_str());
    }
};

// ─── Text input : message texte depuis la PWA ─────────────────────────────────
class TextInputCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0) return;
        Serial.printf("[BLE] TextInput: %s\n", val.c_str());
        if (_text_input_cb) _text_input_cb(val.c_str());
        if (_music_cb)      _music_cb(val.c_str());  // compat music app
    }
};

// ─── Helper notify ────────────────────────────────────────────────────────────
static void notify_char(BLECharacteristic *c, const char *json) {
    if (!_connected || !c || !json) return;
    static char buf[MTU_MAX];
    strlcpy(buf, json, sizeof(buf));
    c->setValue((uint8_t *)buf, strlen(buf));
    c->notify();
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void ble_mgr_init() {
    BLEDevice::init(DEVICE_NAME);
    BLEDevice::setMTU(MTU_MAX);
    _server = BLEDevice::createServer();
    _server->setCallbacks(new ConnCB());

    BLEService *svc = _server->createService(BLEUUID(SERVICE_UUID), BLE_HANDLE_COUNT);

    _c_wifi_scan = svc->createCharacteristic(
        CHAR_WIFI_SCAN,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY);
    _c_wifi_scan->setCallbacks(new WifiScanCB());
    _c_wifi_scan->addDescriptor(new BLE2902());

    _c_wifi_prov = svc->createCharacteristic(
        CHAR_WIFI_PROV,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR);
    _c_wifi_prov->setCallbacks(new WifiProvCB());

    _c_agent_sync = svc->createCharacteristic(
        CHAR_AGENT_SYNC,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY);
    _c_agent_sync->setCallbacks(new AgentSyncCB());
    _c_agent_sync->addDescriptor(new BLE2902());

    _c_text_input = svc->createCharacteristic(
        CHAR_TEXT_INPUT,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR);
    _c_text_input->setCallbacks(new TextInputCB());

    _c_llm_relay = svc->createCharacteristic(
        CHAR_LLM_RELAY,
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_READ);
    _c_llm_relay->addDescriptor(new BLE2902());

    _c_device_status = svc->createCharacteristic(
        CHAR_DEVICE_STATUS,
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_READ);
    _c_device_status->addDescriptor(new BLE2902());

    _c_gps = svc->createCharacteristic(
        CHAR_GPS,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR);
    _c_gps->setCallbacks(new GpsCB());

    svc->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    _active = true;
    Serial.println("[BLE] Advertising : " DEVICE_NAME);
}

// ─── Tick (appelé depuis loop) — résultat scan WiFi ──────────────────────────
void ble_mgr_tick() {
    if (!_scanning) return;
    int n = WiFi.scanComplete();
    if (n < 0) return;
    _scanning = false;

    const int MAX_NETS = 20;
    struct { int rssi; int idx; } nets[MAX_NETS];
    int count = (n < MAX_NETS) ? n : MAX_NETS;
    for (int i = 0; i < count; i++) nets[i] = { WiFi.RSSI(i), i };

    for (int i = 1; i < count; i++) {
        auto key = nets[i]; int j = i - 1;
        while (j >= 0 && nets[j].rssi < key.rssi) { nets[j+1] = nets[j]; j--; }
        nets[j+1] = key;
    }

    int lim = (count < 10) ? count : 10;
    String json = "[";
    for (int i = 0; i < lim; i++) {
        String ssid = WiFi.SSID(nets[i].idx);
        ssid.replace("\\", "\\\\"); ssid.replace("\"", "\\\"");
        if (i > 0) json += ",";
        json += "{\"s\":\"" + ssid + "\",\"r\":" + String(nets[i].rssi) + "}";
    }
    json += "]";

    notify_char(_c_wifi_scan, json.c_str());
    WiFi.scanDelete();
    Serial.printf("[BLE] Scan WiFi : %d reseaux notifies\n", n);
}

bool ble_mgr_connected() { return _connected; }
bool ble_mgr_is_active() { return _active; }

bool ble_mgr_get_gps(double *lat, double *lon) {
    if (!_has_gps) return false;
    *lat = _lat; *lon = _lon;
    return true;
}

void ble_mgr_notify_llm(const char *json)           { notify_char(_c_llm_relay,     json); }
void ble_mgr_notify_device_status(const char *json) { notify_char(_c_device_status, json); }
void ble_mgr_notify_agent_sync(const char *json)    { notify_char(_c_agent_sync,    json); }
void ble_mgr_notify_wifi_scan(const char *json)     { notify_char(_c_wifi_scan,     json); }

void ble_mgr_set_text_input_cb(text_input_cb_t cb) { _text_input_cb = cb; }
void ble_mgr_set_agent_sync_cb(agent_sync_cb_t cb) { _agent_sync_cb = cb; }
void ble_mgr_set_wifi_prov_cb(wifi_prov_cb_t  cb)  { _wifi_prov_cb  = cb; }
void ble_mgr_set_music_cb(music_cmd_cb_t cb)        { _music_cb = cb; }
void ble_mgr_music_notify(const char *json)         { notify_char(_c_llm_relay, json); }
