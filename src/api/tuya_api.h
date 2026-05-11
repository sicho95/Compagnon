#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
// Tuya Cloud API Client
// Docs: https://developer.tuya.com/en/docs/iot/
//
// Setup:
//  1. https://iot.tuya.com → créer un projet Cloud
//  2. Récupérer CLIENT_ID + CLIENT_SECRET
//  3. Dans le projet : "Link Tuya App Account" → scanner le QR avec SmartLife
//     Cela donne accès à TOUS tes devices SmartLife depuis l'API
//  4. Remplir TUYA_CLIENT_ID / TUYA_CLIENT_SECRET dans config.h
// ============================================================

#define TUYA_ENDPOINT             "https://openapi.tuyaeu.com"
#define TUYA_TOKEN_PATH           "/v1.0/token?grant_type=1"
#define TUYA_USER_INFO_PATH       "/v1.0/token/%s"           // GET → uid dans result.uid (via token info)
#define TUYA_USER_DEVICES_PATH    "/v1.0/users/%s/devices"   // GET liste tous les devices d'un uid
#define TUYA_DEVICE_STATUS_PATH   "/v1.0/devices/%s/status"
#define TUYA_DEVICE_COMMAND_PATH  "/v1.0/devices/%s/commands"

// Catégories Tuya utiles
#define TUYA_CAT_LIGHT    "dj"   // ampoule
#define TUYA_CAT_STRIP    "dd"   // ruban LED
#define TUYA_CAT_SWITCH   "kg"   // interrupteur
#define TUYA_CAT_PLUG     "cz"   // prise
#define TUYA_CAT_SENSOR_T "wsdcg" // capteur temp+hum
#define TUYA_CAT_SENSOR_H "ywbj"  // détecteur fumée
#define TUYA_CAT_CURTAIN  "cl"   // volet

struct TuyaDevice {
    String id;
    String name;
    String category;    // "dj", "cz", "wsdcg"...
    String productName;
    bool   online;
    // Status live (rempli par getDeviceStatus)
    bool   is_on;
    int    brightness;  // 10-1000
    int    colorTemp;   // 0-1000
    float  temp_c;
    float  humidity;
};

class TuyaAPI {
public:
    TuyaAPI(const char* clientId, const char* clientSecret);

    // --- Auth ---
    bool    refreshToken();     // à appeler au démarrage et toutes les ~2h
    bool    isTokenValid();
    String  getAccessToken() { return _accessToken; }

    // --- Compte & liste devices ---
    // Récupère l'UID de l'utilisateur lié au projet (nécessaire pour listDevices)
    bool    getCurrentUserUid(String& uid);
    // Liste TOUS les devices du compte SmartLife lié (à l'index page_no, 50 max par page)
    bool    listDevices(const String& uid, std::vector<TuyaDevice>& devices,
                        int pageNo = 0, int pageSize = 50);

    // --- Contrôle device ---
    bool    getDeviceStatus(const char* deviceId, TuyaDevice& out);
    bool    switchDevice(const char* deviceId, bool on);
    bool    setBrightness(const char* deviceId, int brightness); // 10-1000
    bool    setColorTemp(const char* deviceId, int colorTemp);   // 0-1000

    // --- Capteur ---
    bool    getSensorData(const char* deviceId, float& tempC, float& humidity);

private:
    String          _clientId;
    String          _clientSecret;
    String          _accessToken;
    unsigned long   _tokenExpiry;

    bool  _get(const String& path, JsonDocument& response);
    bool  _post(const String& path, const String& body, JsonDocument& response);
    void  _addAuthHeaders(HTTPClient& http, const String& path,
                          const String& method, const String& body = "");
    bool  _sendCommand(const char* deviceId, JsonArray& commands);
};
