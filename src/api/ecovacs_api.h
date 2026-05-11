#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
// Ecovacs Open Platform API — X8 Pro Omni
// Docs: https://open.ecovacs.com
//
// Setup:
//  1. Aller sur https://open.ecovacs.com → "github获取AK"
//     (GitHub OAuth → génère ton Access Key)
//  2. Remplir ECOVACS_ACCESS_KEY dans config.h
//
// API endpoints (REST):
//   GET  /getDeviceList          → liste robots
//   POST /clean                  → démarrer/pause/arrêter nettoyage
//   POST /charge                 → retour base / stop retour
//   POST /getCleanInfo           → état nettoyage + batterie
//
// cleanSt: s=nettoyage, p=pause, h=idle
// chargeSt: g=retour, i=idle, sc=base, charging=en charge
// stationSt: i=idle, wash=lavage, dry=séchage, dust=collecte
// ============================================================

#define ECOVACS_OPEN_API  "https://api.ecovacs.com"

struct EcovacsRobot {
    String nickname;    // nom du robot dans l'app
};

struct EcovacsState {
    String cleanSt;    // s,p,h,goposition...
    String chargeSt;   // g,i,sc,wc,charging
    String stationSt;  // i,wash,dry,dust,clean
    int    battery;    // % (si dispo dans cleanInfo)
    int    cleanedArea;
    int    cleanedTime;
};

class EcovacsAPI {
public:
    EcovacsAPI(const char* accessKey);

    // Liste les robots du compte
    bool    listRobots(std::vector<EcovacsRobot>& robots);

    // Récupère l'état du robot (nickname = nom dans l'app, supporte fuzzy match)
    bool    getState(const char* nickname, EcovacsState& state);

    // Nettoyage : act = "s" (start), "r" (resume), "p" (pause), "h" (stop)
    bool    clean(const char* nickname, const char* act);

    // Retour base : act = "go-start" (retour), "stopGo" (annuler retour)
    bool    charge(const char* nickname, const char* act);

    // Helpers
    bool    startCleaning(const char* nickname)  { return clean(nickname, "s"); }
    bool    pauseCleaning(const char* nickname)   { return clean(nickname, "p"); }
    bool    stopCleaning(const char* nickname)    { return clean(nickname, "h"); }
    bool    goCharge(const char* nickname)        { return charge(nickname, "go-start"); }
    bool    stopGoCharge(const char* nickname)    { return charge(nickname, "stopGo"); }

    // Traduit cleanSt en texte français
    static const char* cleanStFr(const String& st);
    static const char* chargeStFr(const String& st);
    static const char* stationStFr(const String& st);

private:
    String  _accessKey;
    bool    _post(const String& endpoint, JsonDocument& body, JsonDocument& response);
    bool    _get(const String& endpoint, JsonDocument& response);
};
