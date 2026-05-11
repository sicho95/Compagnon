#include "ota.h"
#include <ArduinoOTA.h>

// Mot de passe OTA local — pas de secrets.h, tout est dans NVS pour les clés API
#define OTA_HOSTNAME  "compagnon"
#define OTA_PASSWORD  "nestor_ota"

void net_ota_init() {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]()  { Serial.println("[OTA] Mise a jour..."); });
    ArduinoOTA.onEnd([]()    { Serial.println("[OTA] Termine — reboot"); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        Serial.printf("[OTA] %u%%\n", done * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[OTA] Erreur %u\n", (unsigned)e);
    });

    ArduinoOTA.begin();
    Serial.println("[OTA] Pret — hostname: " OTA_HOSTNAME " port: 3232 mdp: " OTA_PASSWORD);
}

void net_ota_tick() {
    ArduinoOTA.handle();
}
