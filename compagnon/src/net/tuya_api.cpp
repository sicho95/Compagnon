/**
 * tuya_api.cpp — Client HTTPS Tuya OpenAPI v2.0 (EU)
 *
 * Auth : HMAC-SHA256 sur sign = CLIENT_ID + timestamp + nonce + stringToSign
 * stringToSign = HTTPMethod + "\n" + sha256(body) + "\n" + headers + "\n" + url
 * Ref : https://developer.tuya.com/en/docs/cloud/getting-started
 */
#include "tuya_api.h"
#include "net_utils.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <mbedtls/md.h>
#include <Arduino.h>
#include <time.h>
#include <string.h>

#define TUYA_HOST   "https://openapi.tuyaeu.com"
#define TOKEN_TTL   7100000UL   // 7100 s en ms (token valide 7200 s)

static char _client_id[48]     = {};
static char _client_secret[48] = {};
static char _access_token[128] = {};
static unsigned long _token_ts = 0;

// ─── HMAC-SHA256 hex ─────────────────────────────────────────────────────────
static void hmac_sha256_hex(const char *key, const char *data, char *out64) {
    uint8_t hmac[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t *)key, strlen(key));
    mbedtls_md_hmac_update(&ctx, (const uint8_t *)data, strlen(data));
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);
    for (int i = 0; i < 32; i++) sprintf(out64 + 2*i, "%02X", hmac[i]);
    out64[64] = '\0';
}

// ─── SHA256 hex d'un body (vide → SHA256 vide) ────────────────────────────────
static void sha256_hex(const char *data, char *out64) {
    uint8_t hash[32];
    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 0);
    mbedtls_md_starts(&ctx);
    if (data && data[0]) mbedtls_md_update(&ctx, (const uint8_t *)data, strlen(data));
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    for (int i = 0; i < 32; i++) sprintf(out64 + 2*i, "%02X", hash[i]);
    out64[64] = '\0';
}

// ─── Construit la signature Tuya ──────────────────────────────────────────────
static void build_sign(const char *method, const char *path, const char *body,
                       const char *ts, const char *nonce, bool use_token,
                       char *sign_out) {
    char body_hash[65];
    sha256_hex(body, body_hash);

    char sts[512];
    snprintf(sts, sizeof(sts), "%s\n%s\n\n%s", method, body_hash, path);

    char str[640];
    if (use_token)
        snprintf(str, sizeof(str), "%s%s%s%s%s", _client_id, _access_token, ts, nonce, sts);
    else
        snprintf(str, sizeof(str), "%s%s%s%s", _client_id, ts, nonce, sts);

    hmac_sha256_hex(_client_secret, str, sign_out);
}

// ─── Requête générique GET/POST via net_utils (NetworkClientSecure heap) ──────
static int tuya_request(const char *method, const char *path, const char *body,
                         char *resp, size_t resp_len) {
    if (!WiFi.isConnected()) return -1;

    unsigned long ts_ms = (unsigned long)(time(nullptr)) * 1000UL;
    char ts[14];
    snprintf(ts, sizeof(ts), "%lu", ts_ms);
    char nonce[9] = "00000001";

    char sign[65];
    bool use_token = (_access_token[0] != '\0');
    build_sign(method, path, body ? body : "", ts, nonce, use_token, sign);

    // Construction de l'URL complète
    String url = String(TUYA_HOST) + path;

    // Headers supplémentaires au format "Key: Value\nKey2: Value2"
    String extra_headers = String("client_id: ") + _client_id + "\n"
                         + "sign: " + sign + "\n"
                         + "t: " + ts + "\n"
                         + "sign_method: HMAC-SHA256\n"
                         + "nonce: " + nonce + "\n"
                         + "Content-Type: application/json";
    if (use_token)
        extra_headers += String("\naccess_token: ") + _access_token;

    int rc;
    String result;
    if (strcmp(method, "POST") == 0)
        result = https_post_ex(url.c_str(), body ? body : "", extra_headers.c_str(), &rc);
    else
        result = https_get_ex(url.c_str(), extra_headers.c_str(), &rc);

    if (rc == 200 && resp) {
        strncpy(resp, result.c_str(), resp_len - 1);
        resp[resp_len - 1] = '\0';
    }
    Serial.printf("[TUYA] %s %s → HTTP %d\n", method, path, rc);
    return rc;
}

// ─── API publique ─────────────────────────────────────────────────────────────
void tuya_api_init(const char *client_id, const char *client_secret) {
    strlcpy(_client_id,     client_id,     sizeof(_client_id));
    strlcpy(_client_secret, client_secret, sizeof(_client_secret));
    _access_token[0] = '\0';
    _token_ts = 0;
    Serial.println("[TUYA] Init OK");
}

bool tuya_api_get_token() {
    char resp[512];
    int rc = tuya_request("GET", "/v1.0/token?grant_type=1", nullptr, resp, sizeof(resp));
    if (rc != 200) return false;

    JsonDocument doc;
    if (deserializeJson(doc, resp) != DeserializationError::Ok) return false;
    if (!doc["success"] | false) {
        Serial.printf("[TUYA] token error: %s\n", doc["msg"] | "?");
        return false;
    }
    const char *tok = doc["result"]["access_token"] | "";
    if (!tok[0]) return false;
    strlcpy(_access_token, tok, sizeof(_access_token));
    _token_ts = millis();
    Serial.printf("[TUYA] Token OK (%d chars)\n", (int)strlen(_access_token));
    return true;
}

bool tuya_api_is_ready() {
    if (_access_token[0] == '\0') return false;
    if (millis() - _token_ts > TOKEN_TTL) { _access_token[0] = '\0'; return false; }
    return true;
}

bool tuya_api_get_devices(char *out, size_t len) {
    if (!tuya_api_is_ready()) return false;
    return tuya_request("GET", "/v2.0/cloud/thing/device?page_size=20", nullptr, out, len) == 200;
}

bool tuya_api_get_device_status(const char *device_id, char *out, size_t len) {
    if (!tuya_api_is_ready()) return false;
    char path[96];
    snprintf(path, sizeof(path), "/v1.0/iot-03/devices/%s/status", device_id);
    return tuya_request("GET", path, nullptr, out, len) == 200;
}

bool tuya_api_send_command(const char *device_id, const char *code, const char *value) {
    if (!tuya_api_is_ready()) return false;
    char path[96];
    snprintf(path, sizeof(path), "/v1.0/iot-03/devices/%s/commands", device_id);
    char body[256];
    snprintf(body, sizeof(body), "{\"commands\":[{\"code\":\"%s\",\"value\":%s}]}", code, value);
    return tuya_request("POST", path, body, nullptr, 0) == 200;
}

bool tuya_api_send_command_bool(const char *device_id, const char *code, bool value) {
    return tuya_api_send_command(device_id, code, value ? "true" : "false");
}

bool tuya_api_send_command_int(const char *device_id, const char *code, int value) {
    char val[16];
    snprintf(val, sizeof(val), "%d", value);
    return tuya_api_send_command(device_id, code, val);
}
