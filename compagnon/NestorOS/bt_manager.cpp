/**
 * bt_manager.cpp
 * Gestionnaire BLE pour ESP32-S3.
 *
 * Fix v2 : BluetoothSerial n'existe PAS sur ESP32-S3 (pas de BT Classic).
 * Remplacement par BLEDevice (BLE uniquement).
 *
 * Fonctionnement :
 *  - Demarre un serveur BLE sous le nom "Compagnon"
 *  - Advertising on/off via bt_manager_set_visible()
 *  - Pour l'avenir : ajouter Nordic UART Service (NUS) pour
 *    recevoir des commandes texte depuis une app BLE / Web Bluetooth.
 */
#include "bt_manager.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>
#include <Arduino.h>

static bool            _bt_active  = false;
static bool            _visible    = false;
static BLEServer      *_ble_server = nullptr;
static BLEAdvertising *_adv        = nullptr;

class BLEConnCallback : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    Serial.println("[BLE] Client connecte");
  }
  void onDisconnect(BLEServer *s) override {
    Serial.println("[BLE] Client deconnecte");
    if (_visible && _adv) _adv->start();
  }
};

void bt_manager_init() {
  BLEDevice::init("Compagnon");
  _ble_server = BLEDevice::createServer();
  _ble_server->setCallbacks(new BLEConnCallback());

  _adv = BLEDevice::getAdvertising();
  _adv->setScanResponse(true);
  _adv->setMinPreferred(0x06);
  _adv->start();

  _bt_active = true;
  _visible   = true;
  Serial.println("[BLE] Demarre - visible : Compagnon");
}

void bt_manager_tick() {
  // Callbacks BLE geres en arriere-plan par le stack ESP32.
}

bool bt_is_active() {
  return _bt_active;
}

void bt_manager_set_visible(bool visible) {
  _visible = visible;
  if (!_bt_active || !_adv) return;
  if (visible) {
    _adv->start();
    Serial.println("[BLE] Advertising ON");
  } else {
    _adv->stop();
    Serial.println("[BLE] Advertising OFF");
  }
}
