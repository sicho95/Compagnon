#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
// Tuya Cloud API Client
// Docs: https://developer.tuya.com/en/docs/iot/
//
// Setup:
//  1. Create account on https://iot.tuya.com
//  2. Create Cloud Project → get CLIENT_ID + CLIENT_SECRET
//  3. Link your SmartLife account in the project
//  4. Fill TUYA_CLIENT_ID / TUYA_CLIENT_SECRET in config.h
// ============================================================

#define TUYA_ENDPOINT "https://openapi.tuyaeu.com"
#define TUYA_TOKEN_PATH "/v1.0/token?grant_type=1"
#define TUYA_DEVICE_STATUS_PATH "/v1.0/devices/%s/status"
#define TUYA_DEVICE_COMMAND_PATH "/v1.0/devices/%s/commands"

struct TuyaDevice {
    String id;
    String name;
    bool   online;
    bool   is_on;
    int    brightness;  // 0-1000
    float  temp_c;
    float  humidity;
};

class TuyaAPI {
public:
    TuyaAPI(const char* clientId, const char* clientSecret);

    // Auth
    bool            refreshToken();          // call at startup and every ~2h
    bool            isTokenValid();

    // Device control
    bool            getDeviceStatus(const char* deviceId, TuyaDevice& out);
    bool            switchDevice(const char* deviceId, bool on);
    bool            setBrightness(const char* deviceId, int brightness);  // 0-1000
    bool            setColorTemp(const char* deviceId, int colorTemp);    // 0-1000

    // Sensor reading (temp/humidity)
    bool            getSensorData(const char* deviceId, float& tempC, float& humidity);

private:
    String          _clientId;
    String          _clientSecret;
    String          _accessToken;
    unsigned long   _tokenExpiry;  // millis

    String          _buildSign(const String& t, const String& nonce, const String& path, const String& body = "");
    bool            _sendCommand(const char* deviceId, JsonArray& commands);
    bool            _get(const String& path, JsonDocument& response);
    bool            _post(const String& path, const String& body, JsonDocument& response);
    void            _addAuthHeaders(HTTPClient& http, const String& path, const String& method, const String& body = "");
};
