#pragma once
/**
 * bt_manager.h
 * Gestionnaire BLE (Bluetooth Low Energy) pour ESP32-S3.
 * NOTE : L'ESP32-S3 ne supporte PAS BluetoothSerial (BT Classic).
 * Utilisation BLE uniquement via BLEDevice.
 */
void bt_manager_init();
void bt_manager_tick();
bool bt_is_active();
void bt_manager_set_visible(bool visible);
