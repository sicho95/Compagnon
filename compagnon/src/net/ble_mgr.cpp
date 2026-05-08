#include "ble_mgr.h"
#include "../gps.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <WiFi.h>
#include <string.h>
#include <stdlib.h>

// Aligner avec NESTOR_SERVICE et CHAR dans src/bt/ble.js
#define SERVICE_UUID        "12345678-1234-5678-1234-56789ABCDEF0"
#define GPS_CHAR_UUID       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define WIFI_SCAN_CHAR_UUID "12345678-0001-5678-1234-56789ABCDEF0"
#define WIFI_PROV_CHAR_UUID "12345678-0002-5678-1234-56789ABCDEF0"
#define DEVICE_NAME         "Nestor"

static BLEServer         *_server         = nullptr;
static BLECharacteristic *_wifi_scan_char = nullptr;
static bool               _connected      = false;
static bool               _active         = false;
static bool               _scanning       = false;

class ConnCB : public BLEServerCallbacks {
    void onConnect(BLEServer *) override {
        _connected = true;
        Serial.println("[BLE] Telephone connecte");
    }
    void onDisconnect(BLEServer *s) override {
        _connected = false;
        Serial.println("[BLE] Deconnecte — re-advertising");
        s->startAdvertising();
    }
};

// Décoder la position GPS reçue en float32 little-endian (lat 4 oct. + lon 4 oct.)
class GpsCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() < 8) return;
        float lat, lon;
        memcpy(&lat, val.c_str(),     4);
        memcpy(&lon, val.c_str() + 4, 4);
        if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f) return;
        gps_update(lat, lon);
        Serial.printf("[BLE] GPS : %.5f, %.5f\n", lat, lon);
    }
};

// Déclencher un scan WiFi asynchrone sur réception de l'octet 0x01
class WifiScanCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() < 1 || (uint8_t)val[0] != 0x01) return;
        if (_scanning) return;
        _scanning = true;
        WiFi.scanNetworks(true);   // async = non-bloquant
        Serial.println("[BLE] Scan WiFi declenche");
    }
};

// Extraire une valeur string du JSON minimal {"key":"value",...}
static String jsonExtract(const String &src, const char *key) {
    int ki = src.indexOf(key);
    if (ki < 0) return "";
    int start = src.indexOf('"', ki + strlen(key));
    if (start < 0) return "";
    int end = src.indexOf('"', start + 1);
    if (end < 0) return "";
    return src.substring(start + 1, end);
}

// Mettre à jour le réseau WiFi via JSON {"ssid":"...","password":"..."}
class WifiProvCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0) return;
        String ssid = jsonExtract(val, "\"ssid\"");
        String pwd  = jsonExtract(val, "\"password\"");
        if (ssid.length() == 0) return;
        Serial.printf("[BLE] Provisionnement WiFi : SSID=%s\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pwd.c_str());
    }
};

void ble_mgr_init() {
    BLEDevice::init(DEVICE_NAME);
    _server = BLEDevice::createServer();
    _server->setCallbacks(new ConnCB());

    BLEService *svc = _server->createService(SERVICE_UUID);

    // Caractéristique GPS : écriture float32 little-endian (8 octets : lat + lon)
    BLECharacteristic *gps_char = svc->createCharacteristic(
        GPS_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    gps_char->setCallbacks(new GpsCB());

    // Caractéristique WiFi scan : écriture 0x01 déclenche le scan, résultat notifié en JSON
    _wifi_scan_char = svc->createCharacteristic(
        WIFI_SCAN_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _wifi_scan_char->addDescriptor(new BLE2902());
    _wifi_scan_char->setCallbacks(new WifiScanCB());

    // Caractéristique WiFi provision : {"ssid":"...","password":"..."}
    BLECharacteristic *prov_char = svc->createCharacteristic(
        WIFI_PROV_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    prov_char->setCallbacks(new WifiProvCB());

    svc->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    BLEDevice::startAdvertising();

    _active = true;
    Serial.println("[BLE] Advertising : " DEVICE_NAME);
}

void ble_mgr_tick() {
    if (!_scanning) return;
    int n = WiFi.scanComplete();
    if (n < 0) return;   // scan encore en cours (WIFI_SCAN_RUNNING = -1)

    _scanning = false;

    // Construire le JSON des réseaux triés par RSSI décroissant, limité à 10 entrées
    const int MAX_NETS = 20;
    struct { int rssi; int idx; } nets[MAX_NETS];
    int count = (n < MAX_NETS) ? n : MAX_NETS;

    for (int i = 0; i < count; i++) { nets[i] = { WiFi.RSSI(i), i }; }

    // Tri à bulles léger pour classer par signal décroissant
    for (int i = 0; i < count - 1; i++)
        for (int j = i + 1; j < count; j++)
            if (nets[j].rssi > nets[i].rssi) {
                auto t = nets[i]; nets[i] = nets[j]; nets[j] = t;
            }

    int lim = (count < 10) ? count : 10;
    String json = "[";
    for (int i = 0; i < lim; i++) {
        String ssid = WiFi.SSID(nets[i].idx);
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        if (i > 0) json += ",";
        json += "{\"s\":\"" + ssid + "\",\"r\":" + String(nets[i].rssi) + "}";
    }
    json += "]";

    _wifi_scan_char->setValue(json.c_str());
    _wifi_scan_char->notify();
    WiFi.scanDelete();
    Serial.printf("[BLE] Scan WiFi termine : %d reseaux\n", n);
}

bool ble_mgr_connected() { return _connected; }
bool ble_mgr_is_active()  { return _active; }

bool ble_mgr_get_gps(double *lat, double *lon) {
    if (!gps_has_fix()) return false;
    *lat = (double)gps_get_lat();
    *lon = (double)gps_get_lon();
    return true;
}
