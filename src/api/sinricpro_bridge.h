#pragma once
// ============================================================
// SinricPro Bridge — contrôle Alexa via ESP32
// Docs: https://sinric.pro
//
// Setup:
//  1. Créer un compte sur https://sinric.pro (gratuit)
//  2. Créer des devices dans le dashboard SinricPro
//  3. Connecter l'Alexa Skill SinricPro dans l'app Alexa
//  4. Remplir SINRIC_APP_KEY + SINRIC_APP_SECRET dans config.h
//
// Lib Arduino à installer: "SinricPro" par Boris Jaeger
// ============================================================

#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <SinricProDimSwitch.h>

// Callbacks
typedef std::function<void(const String& deviceId, bool state)> SinricSwitchCallback;
typedef std::function<void(const String& deviceId, int power)> SinricDimCallback;

class SinricProBridge {
public:
    SinricProBridge(const char* appKey, const char* appSecret);

    // Enregistrer un device switch simple (prise Alexa)
    void addSwitch(const char* deviceId, SinricSwitchCallback cb);

    // Enregistrer un device dimmer (lumière Alexa)
    void addDimmer(const char* deviceId, SinricDimCallback cb);

    // Mettre à jour l'état du device côté cloud (feedback)
    void reportSwitchState(const char* deviceId, bool state);
    void reportDimmerLevel(const char* deviceId, int power);

    // À appeler dans loop()
    void handle();

    // À appeler dans setup() après WiFi connecté
    void begin();

private:
    const char* _appKey;
    const char* _appSecret;
};
