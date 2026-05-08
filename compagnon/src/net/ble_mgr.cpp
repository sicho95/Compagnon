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

#define SERVICE_UUID         "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define GPS_CHAR_UUID        "BEB5483E-36E1-4688-B7F5-EA07361B26A8"
#define MUSIC_CMD_CHAR_UUID  "A0B1C2D3-0001-2222-3333-AABB12345678"
#define MUSIC_STAT_CHAR_UUID "A0B1C2D3-0002-2222-3333-AABB12345678"
#define DEVICE_NAME          "Compagnon-Nestor"

static BLEServer         *_server      = nullptr;
static BLECharacteristic *_gps_char    = nullptr;
static BLECharacteristic *_music_cmd   = nullptr;
static BLECharacteristic *_music_stat  = nullptr;
static bool               _connected   = false;
static bool               _active      = false;
static double             _lat         = 0.0;
static double             _lon         = 0.0;
static bool               _has_gps     = false;
static music_cmd_cb_t     _music_cb    = nullptr;

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
        if (val.length() == 0) return;
        const char *s = val.c_str();
        char *comma = (char *)strchr(s, ',');
        if (!comma) return;
        *comma = '\0';
        double lat = atof(s);
        double lon = atof(comma + 1);
        *comma = ',';
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return;
        _lat = lat; _lon = lon; _has_gps = true;
        Serial.printf("[BLE] GPS recu : %.5f, %.5f\n", _lat, _lon);
    }
};

// Dispatcher les commandes music: vers l'app Musique
class MusicCmdCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();
        if (val.length() == 0 || !_music_cb) return;
        Serial.printf("[BLE] Music cmd: %s\n", val.c_str());
        _music_cb(val.c_str());
    }
};

void ble_mgr_init() {
    BLEDevice::init(DEVICE_NAME);
    _server = BLEDevice::createServer();
    _server->setCallbacks(new ConnCB());

    BLEService *svc = _server->createService(BLEUUID(SERVICE_UUID), 10);

    _gps_char = svc->createCharacteristic(
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
    _gps_char->addDescriptor(new BLE2902());
    _gps_char->setCallbacks(new GpsCB());

    // Commande music (write depuis la PWA)
    _music_cmd = svc->createCharacteristic(
        MUSIC_CMD_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    _music_cmd->setCallbacks(new MusicCmdCB());

    // Statut/notification music (notify vers la PWA)
    _music_stat = svc->createCharacteristic(
        MUSIC_STAT_CHAR_UUID,
        BLECharacteristic::PROPERTY_NOTIFY |
        BLECharacteristic::PROPERTY_READ
    );
    _music_stat->addDescriptor(new BLE2902());

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
    if (!_has_gps) return false;
    *lat = _lat; *lon = _lon;
    return true;
}

void ble_mgr_set_music_cb(music_cmd_cb_t cb) {
    _music_cb = cb;
}

// Envoyer une notification JSON vers la PWA via la caractéristique music status
void ble_mgr_music_notify(const char *json) {
    if (!_connected || !_music_stat || !json) return;
    int len = strlen(json);
    // Tronquer si nécessaire (MTU typique 512 octets)
    static char buf[512];
    strlcpy(buf, json, sizeof(buf));
    _music_stat->setValue((uint8_t *)buf, strlen(buf));
    _music_stat->notify();
}
