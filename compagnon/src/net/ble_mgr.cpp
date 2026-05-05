#include "ble_mgr.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

#define SERVICE_UUID        "4FAFC201-1FB5-459E-8FCC-C5C9C331914B"
#define GPS_CHAR_UUID       "BEB5483E-36E1-4688-B7F5-EA07361B26A8"
#define DEVICE_NAME         "Compagnon-Nestor"

static BLEServer        *_server    = nullptr;
static BLECharacteristic *_gps_char = nullptr;
static bool              _connected = false;
static bool              _active    = false;
static double            _lat       = 0.0;
static double            _lon       = 0.0;
static bool              _has_gps   = false;

class ConnCB : public BLEServerCallbacks {
    void onConnect(BLEServer *) override {
        _connected = true;
        Serial.println("[BLE] Telephone connecte");
    }
    void onDisconnect(BLEServer *s) override {
        _connected = false;
        Serial.println("[BLE] Telephone deconnecte — re-advertising");
        s->startAdvertising();
    }
};

class GpsCB : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *c) override {
        String val = c->getValue();       // String Arduino
        if (val.length() == 0) return;    // API Arduino
        // Format attendu : "lat,lon"  ex "48.8566,2.3522"
        const char *s = val.c_str();
        char *comma = (char *)strchr(s, ',');
        if (!comma) return;
        *comma = '\0';
        double lat = atof(s);
        double lon = atof(comma + 1);
        *comma = ',';
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return;
        _lat = lat;
        _lon = lon;
        _has_gps = true;
        Serial.printf("[BLE] GPS recu : %.5f, %.5f\n", _lat, _lon);
    }
};

void ble_mgr_init() {
    BLEDevice::init(DEVICE_NAME);
    _server = BLEDevice::createServer();
    _server->setCallbacks(new ConnCB());

    BLEService *svc = _server->createService(SERVICE_UUID);
    _gps_char = svc->createCharacteristic(
        GPS_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    _gps_char->addDescriptor(new BLE2902());
    _gps_char->setCallbacks(new GpsCB());
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
    // Pas d'action bloquante nécessaire — les callbacks BLE sont async
}

bool ble_mgr_connected()  { return _connected; }
bool ble_mgr_is_active()  { return _active; }

bool ble_mgr_get_gps(double *lat, double *lon) {
    if (!_has_gps) return false;
    *lat = _lat;
    *lon = _lon;
    return true;
}
