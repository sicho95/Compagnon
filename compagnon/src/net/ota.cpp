#include "ota.h"
#include <ArduinoOTA.h>

#define OTA_HOSTNAME  "compagnon"
#define OTA_PASS      "nestor_ota"   // TODO V2: lire depuis NVS

void net_ota_init() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASS);

    ArduinoOTA.onStart([]()  { Serial.println("[OTA] Mise a jour..."); });
    ArduinoOTA.onEnd([]()    { Serial.println("[OTA] Termine — reboot"); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        Serial.printf("[OTA] %u%%\n", done * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Erreur %u\n", (unsigned)e);
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Pret — hostname: " OTA_HOSTNAME " port: 3232 mdp: " OTA_PASS);
}

void net_ota_tick() {
    ArduinoOTA.handle();
}
