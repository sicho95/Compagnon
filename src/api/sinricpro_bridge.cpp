#include "sinricpro_bridge.h"
#include <Arduino.h>

SinricProBridge::SinricProBridge(const char* appKey, const char* appSecret)
    : _appKey(appKey), _appSecret(appSecret) {}

void SinricProBridge::begin() {
    SinricPro.begin(_appKey, _appSecret);
    Serial.println("[SinricPro] Initialized");
}

void SinricProBridge::addSwitch(const char* deviceId, SinricSwitchCallback cb) {
    SinricProSwitch& sw = SinricPro[deviceId];
    sw.onPowerState([cb, deviceId](const String& id, bool& state) {
        cb(id, state);
        return true;
    });
    Serial.printf("[SinricPro] Switch registered: %s\n", deviceId);
}

void SinricProBridge::addDimmer(const char* deviceId, SinricDimCallback cb) {
    SinricProDimSwitch& dim = SinricPro[deviceId];
    dim.onPowerLevel([cb, deviceId](const String& id, int& power) {
        cb(id, power);
        return true;
    });
    Serial.printf("[SinricPro] Dimmer registered: %s\n", deviceId);
}

void SinricProBridge::reportSwitchState(const char* deviceId, bool state) {
    SinricProSwitch& sw = SinricPro[deviceId];
    sw.sendPowerStateEvent(state);
}

void SinricProBridge::reportDimmerLevel(const char* deviceId, int power) {
    SinricProDimSwitch& dim = SinricPro[deviceId];
    dim.sendPowerLevelEvent(power);
}

void SinricProBridge::handle() {
    SinricPro.handle();
}
