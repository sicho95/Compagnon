#include "tuya_api.h"
#include <mbedtls/md.h>
#include <WiFiClientSecure.h>
#include <vector>

// ---- SHA256 hex ----
static String sha256hex(const String& data) {
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    String r; for (int i=0;i<32;i++){char h[3];sprintf(h,"%02x",hash[i]);r+=h;} return r;
}

// ---- HMAC-SHA256 hex (uppercase) ----
static String hmac256hex(const String& key, const String& data) {
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)data.c_str(), data.length());
    mbedtls_md_hmac_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    String r; for (int i=0;i<32;i++){char h[3];sprintf(h,"%02X",hash[i]);r+=h;} return r;
}

TuyaAPI::TuyaAPI(const char* clientId, const char* clientSecret)
    : _clientId(clientId), _clientSecret(clientSecret), _tokenExpiry(0) {}

// ---- Token ----

bool TuyaAPI::refreshToken() {
    JsonDocument doc;
    if (!_get(TUYA_TOKEN_PATH, doc)) return false;
    if (!doc["success"].as<bool>()) return false;
    _accessToken = doc["result"]["access_token"].as<String>();
    int exp = doc["result"]["expire_time"].as<int>();
    _tokenExpiry = millis() + ((unsigned long)(exp - 60) * 1000UL);
    Serial.printf("[Tuya] Token OK, expire dans %ds\n", exp);
    return true;
}

bool TuyaAPI::isTokenValid() {
    return _accessToken.length() > 0 && millis() < _tokenExpiry;
}

// ---- Headers auth (sign Tuya HMAC-SHA256) ----

void TuyaAPI::_addAuthHeaders(HTTPClient& http, const String& path,
                               const String& method, const String& body) {
    // Tuya t = epoch secondes. On approx avec millis + offset fixe
    // En production, synchroniser avec NTP et utiliser time(nullptr)
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    String t = String((long long)tv.tv_sec * 1000LL + tv.tv_usec / 1000LL);

    String nonce = String(esp_random(), HEX);
    String bodyHash = sha256hex(body);
    String strToSign = method + "\n" + bodyHash + "\n" + "" + "\n" + path;
    String token = isTokenValid() ? _accessToken : "";
    String signStr = _clientId + token + t + nonce + strToSign;
    String sign = hmac256hex(_clientSecret, signStr);

    http.addHeader("client_id",    _clientId);
    http.addHeader("access_token", token);
    http.addHeader("t",            t);
    http.addHeader("nonce",        nonce);
    http.addHeader("sign",         sign);
    http.addHeader("sign_method",  "HMAC-SHA256");
    http.addHeader("Content-Type", "application/json");
}

// ---- GET ----

bool TuyaAPI::_get(const String& path, JsonDocument& response) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, String(TUYA_ENDPOINT) + path);
    _addAuthHeaders(http, path, "GET");
    int code = http.GET();
    if (code != 200) { Serial.printf("[Tuya] GET %s -> %d\n", path.c_str(), code); http.end(); return false; }
    auto err = deserializeJson(response, http.getString());
    http.end();
    return err == DeserializationError::Ok;
}

// ---- POST ----

bool TuyaAPI::_post(const String& path, const String& body, JsonDocument& response) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http;
    http.begin(client, String(TUYA_ENDPOINT) + path);
    _addAuthHeaders(http, path, "POST", body);
    int code = http.POST(body);
    if (code != 200) { Serial.printf("[Tuya] POST %s -> %d\n", path.c_str(), code); http.end(); return false; }
    auto err = deserializeJson(response, http.getString());
    http.end();
    return err == DeserializationError::Ok;
}

// ---- Get current user UID ----
// Tuya: GET /v1.0/token/{access_token} retourne les infos du token dont l'uid

bool TuyaAPI::getCurrentUserUid(String& uid) {
    if (!isTokenValid()) return false;
    char path[128];
    snprintf(path, sizeof(path), "/v1.0/token/%s", _accessToken.c_str());
    JsonDocument doc;
    if (!_get(path, doc)) return false;
    if (!doc["success"].as<bool>()) return false;
    uid = doc["result"]["uid"].as<String>();
    return uid.length() > 0;
}

// ---- List ALL devices of linked SmartLife account ----

bool TuyaAPI::listDevices(const String& uid, std::vector<TuyaDevice>& devices,
                           int pageNo, int pageSize) {
    char path[128];
    snprintf(path, sizeof(path), "/v1.0/users/%s/devices?page_no=%d&page_size=%d",
             uid.c_str(), pageNo, pageSize);
    JsonDocument doc;
    if (!_get(path, doc)) return false;
    if (!doc["success"].as<bool>()) return false;

    devices.clear();
    JsonArray result = doc["result"].as<JsonArray>();
    for (JsonObject item : result) {
        TuyaDevice dev;
        dev.id          = item["id"].as<String>();
        dev.name        = item["name"].as<String>();
        dev.category    = item["category"].as<String>();
        dev.productName = item["product_name"].as<String>();
        dev.online      = item["online"].as<bool>();
        dev.is_on       = false;
        dev.brightness  = 500;
        dev.colorTemp   = 500;
        dev.temp_c      = 0;
        dev.humidity    = 0;
        // Status inline
        JsonArray status = item["status"].as<JsonArray>();
        for (JsonObject s : status) {
            String code = s["code"].as<String>();
            if      (code=="switch_led"||code=="switch") dev.is_on = s["value"].as<bool>();
            else if (code=="bright_value_v2")            dev.brightness = s["value"].as<int>();
            else if (code=="temp_value_v2")              dev.colorTemp = s["value"].as<int>();
            else if (code=="temp_current")               dev.temp_c = s["value"].as<float>()/10.f;
            else if (code=="humidity_value")             dev.humidity = s["value"].as<float>()/10.f;
        }
        devices.push_back(dev);
    }
    Serial.printf("[Tuya] %d devices trouvés (page %d)\n", (int)devices.size(), pageNo);
    return true;
}

// ---- Device status (refresh live) ----

bool TuyaAPI::getDeviceStatus(const char* deviceId, TuyaDevice& out) {
    char path[128];
    snprintf(path, sizeof(path), TUYA_DEVICE_STATUS_PATH, deviceId);
    JsonDocument doc;
    if (!_get(path, doc)) return false;
    if (!doc["success"].as<bool>()) return false;
    JsonArray result = doc["result"].as<JsonArray>();
    for (JsonObject item : result) {
        String code = item["code"].as<String>();
        if      (code=="switch_led"||code=="switch") out.is_on = item["value"].as<bool>();
        else if (code=="bright_value_v2")            out.brightness = item["value"].as<int>();
        else if (code=="temp_value_v2")              out.colorTemp = item["value"].as<int>();
        else if (code=="temp_current")               out.temp_c = item["value"].as<float>()/10.f;
        else if (code=="humidity_value")             out.humidity = item["value"].as<float>()/10.f;
    }
    return true;
}

// ---- Switch ----
bool TuyaAPI::switchDevice(const char* deviceId, bool on) {
    JsonDocument d; JsonArray cmds = d.to<JsonArray>();
    JsonObject c = cmds.add<JsonObject>();
    c["code"] = "switch_led"; c["value"] = on;
    return _sendCommand(deviceId, cmds);
}

// ---- Brightness ----
bool TuyaAPI::setBrightness(const char* deviceId, int brightness) {
    JsonDocument d; JsonArray cmds = d.to<JsonArray>();
    JsonObject c = cmds.add<JsonObject>();
    c["code"] = "bright_value_v2"; c["value"] = constrain(brightness, 10, 1000);
    return _sendCommand(deviceId, cmds);
}

// ---- Color Temp ----
bool TuyaAPI::setColorTemp(const char* deviceId, int colorTemp) {
    JsonDocument d; JsonArray cmds = d.to<JsonArray>();
    JsonObject c = cmds.add<JsonObject>();
    c["code"] = "temp_value_v2"; c["value"] = constrain(colorTemp, 0, 1000);
    return _sendCommand(deviceId, cmds);
}

// ---- Sensor ----
bool TuyaAPI::getSensorData(const char* deviceId, float& tempC, float& humidity) {
    TuyaDevice dev;
    if (!getDeviceStatus(deviceId, dev)) return false;
    tempC = dev.temp_c; humidity = dev.humidity;
    return true;
}

// ---- Send command ----
bool TuyaAPI::_sendCommand(const char* deviceId, JsonArray& commands) {
    char path[128];
    snprintf(path, sizeof(path), TUYA_DEVICE_COMMAND_PATH, deviceId);
    JsonDocument b; b["commands"] = commands;
    String body; serializeJson(b, body);
    JsonDocument resp;
    if (!_post(path, body, resp)) return false;
    return resp["success"].as<bool>();
}
