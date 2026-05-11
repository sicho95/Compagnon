#include "tuya_api.h"
#include <mbedtls/md.h>   // HMAC-SHA256 built-in ESP32
#include <WiFiClientSecure.h>

// ---- Helpers ----

static String sha256hex(const String& data) {
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(type), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    String result;
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        result += hex;
    }
    return result;
}

static String hmac256hex(const String& key, const String& data) {
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_hmac_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    String result;
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02X", hash[i]);
        result += hex;
    }
    return result;
}

// ---- Constructor ----

TuyaAPI::TuyaAPI(const char* clientId, const char* clientSecret)
    : _clientId(clientId), _clientSecret(clientSecret), _tokenExpiry(0) {}

// ---- Auth ----

bool TuyaAPI::refreshToken() {
    JsonDocument doc;
    if (!_get(TUYA_TOKEN_PATH, doc)) return false;
    if (!doc["success"].as<bool>()) return false;
    _accessToken = doc["result"]["access_token"].as<String>();
    int expiresIn = doc["result"]["expire_time"].as<int>();  // seconds
    _tokenExpiry = millis() + ((unsigned long)(expiresIn - 60) * 1000UL);
    Serial.println("[Tuya] Token refreshed");
    return true;
}

bool TuyaAPI::isTokenValid() {
    return _accessToken.length() > 0 && millis() < _tokenExpiry;
}

// ---- Auth header builder ----

void TuyaAPI::_addAuthHeaders(HTTPClient& http, const String& path, const String& method, const String& body) {
    String t = String(millis() / 1000UL + 1700000000UL);  // rough epoch
    String nonce = String(esp_random(), HEX);
    String bodyHash = sha256hex(body);

    // StringToSign = method + "\n" + bodyHash + "\n" + "" + "\n" + path
    String strToSign = method + "\n" + bodyHash + "\n" + "" + "\n" + path;

    String tokenPart = isTokenValid() ? _accessToken : "";
    String signStr = _clientId + tokenPart + t + nonce + strToSign;
    String sign = hmac256hex(_clientSecret, signStr);

    http.addHeader("client_id", _clientId);
    http.addHeader("access_token", tokenPart);
    http.addHeader("t", t);
    http.addHeader("nonce", nonce);
    http.addHeader("sign", sign);
    http.addHeader("sign_method", "HMAC-SHA256");
    http.addHeader("Content-Type", "application/json");
}

// ---- GET ----

bool TuyaAPI::_get(const String& path, JsonDocument& response) {
    WiFiClientSecure client;
    client.setInsecure();  // TODO: add proper CA in production
    HTTPClient http;
    String url = String(TUYA_ENDPOINT) + path;
    http.begin(client, url);
    _addAuthHeaders(http, path, "GET");
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Tuya] GET %s -> %d\n", path.c_str(), code);
        http.end();
        return false;
    }
    String payload = http.getString();
    http.end();
    DeserializationError err = deserializeJson(response, payload);
    return err == DeserializationError::Ok;
}

// ---- POST ----

bool TuyaAPI::_post(const String& path, const String& body, JsonDocument& response) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(TUYA_ENDPOINT) + path;
    http.begin(client, url);
    _addAuthHeaders(http, path, "POST", body);
    int code = http.POST(body);
    if (code != 200) {
        Serial.printf("[Tuya] POST %s -> %d\n", path.c_str(), code);
        http.end();
        return false;
    }
    String payload = http.getString();
    http.end();
    DeserializationError err = deserializeJson(response, payload);
    return err == DeserializationError::Ok;
}

// ---- Device Status ----

bool TuyaAPI::getDeviceStatus(const char* deviceId, TuyaDevice& out) {
    char path[128];
    snprintf(path, sizeof(path), TUYA_DEVICE_STATUS_PATH, deviceId);
    JsonDocument doc;
    if (!_get(path, doc)) return false;
    if (!doc["success"].as<bool>()) return false;
    out.id = deviceId;
    out.online = true;
    JsonArray result = doc["result"].as<JsonArray>();
    for (JsonObject item : result) {
        String code = item["code"].as<String>();
        if (code == "switch_led" || code == "switch")    out.is_on = item["value"].as<bool>();
        else if (code == "bright_value_v2")               out.brightness = item["value"].as<int>();
        else if (code == "temp_current")                  out.temp_c = item["value"].as<float>() / 10.0f;
        else if (code == "humidity_value")                out.humidity = item["value"].as<float>() / 10.0f;
    }
    return true;
}

// ---- Switch ----

bool TuyaAPI::switchDevice(const char* deviceId, bool on) {
    JsonDocument cmdDoc;
    JsonArray commands = cmdDoc.to<JsonArray>();
    JsonObject cmd = commands.add<JsonObject>();
    cmd["code"] = "switch_led";
    cmd["value"] = on;
    return _sendCommand(deviceId, commands);
}

// ---- Brightness ----

bool TuyaAPI::setBrightness(const char* deviceId, int brightness) {
    JsonDocument cmdDoc;
    JsonArray commands = cmdDoc.to<JsonArray>();
    JsonObject cmd = commands.add<JsonObject>();
    cmd["code"] = "bright_value_v2";
    cmd["value"] = constrain(brightness, 10, 1000);
    return _sendCommand(deviceId, commands);
}

// ---- Color Temp ----

bool TuyaAPI::setColorTemp(const char* deviceId, int colorTemp) {
    JsonDocument cmdDoc;
    JsonArray commands = cmdDoc.to<JsonArray>();
    JsonObject cmd = commands.add<JsonObject>();
    cmd["code"] = "temp_value_v2";
    cmd["value"] = constrain(colorTemp, 0, 1000);
    return _sendCommand(deviceId, commands);
}

// ---- Sensor ----

bool TuyaAPI::getSensorData(const char* deviceId, float& tempC, float& humidity) {
    TuyaDevice dev;
    if (!getDeviceStatus(deviceId, dev)) return false;
    tempC = dev.temp_c;
    humidity = dev.humidity;
    return true;
}

// ---- Send Command ----

bool TuyaAPI::_sendCommand(const char* deviceId, JsonArray& commands) {
    char path[128];
    snprintf(path, sizeof(path), TUYA_DEVICE_COMMAND_PATH, deviceId);
    JsonDocument bodyDoc;
    bodyDoc["commands"] = commands;
    String body;
    serializeJson(bodyDoc, body);
    JsonDocument response;
    if (!_post(path, body, response)) return false;
    return response["success"].as<bool>();
}
