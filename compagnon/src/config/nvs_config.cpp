/**
 * nvs_config.cpp — Implémentation lecture/écriture clés API en NVS
 * Namespace unique : NVS_NAMESPACE = "compagnon"
 */
#include "nvs_config.h"
#include <Arduino.h>
#include <Preferences.h>

static Preferences _nvs;

static const char *KNOWN_KEYS[] = {
  NVS_KEY_GROQ,
  NVS_KEY_GEMINI,
  NVS_KEY_SERPER,
  NVS_KEY_OPENROUTER,
  NVS_KEY_TWELVEDATA,
  NVS_KEY_METEO,
  NVS_KEY_SPOTIFY_ID,
  NVS_KEY_SPOTIFY_SEC,
  NVS_KEY_TUYA_ID,
  NVS_KEY_TUYA_SEC,
  NVS_KEY_ECOVACS_U,
  NVS_KEY_ECOVACS_P,
  nullptr
};

void nvs_config_init() {
  _nvs.begin(NVS_NAMESPACE, false);
  if (!_nvs.isKey("_init")) {
    _nvs.putUChar("_init", 1);
    Serial.printf("[NVS] Namespace '%s' cree (premiere utilisation)\n", NVS_NAMESPACE);
  } else {
    Serial.printf("[NVS] Namespace '%s' OK\n", NVS_NAMESPACE);
  }
  _nvs.end();
}

bool nvs_get_api_key(const char *key_name, char *out, size_t out_len) {
  _nvs.begin(NVS_NAMESPACE, false);
  if (!_nvs.isKey(key_name)) {
    _nvs.end();
    Serial.printf("[NVS] cle absente : '%s'\n", key_name);
    if (out && out_len > 0) out[0] = '\0';
    return false;
  }
  String val = _nvs.getString(key_name, "");
  _nvs.end();
  if (val.length() == 0) {
    if (out && out_len > 0) out[0] = '\0';
    return false;
  }
  strncpy(out, val.c_str(), out_len - 1);
  out[out_len - 1] = '\0';
  return true;
}

bool nvs_set_api_key(const char *key_name, const char *value) {
  if (!value || value[0] == '\0') return false;
  _nvs.begin(NVS_NAMESPACE, false);
  bool ok = _nvs.putString(key_name, value);
  _nvs.end();
  Serial.printf("[NVS] %s '%s' (%d chars)\n", ok ? "stockee" : "ERREUR", key_name, (int)strlen(value));
  return ok;
}

bool nvs_has_api_key(const char *key_name) {
  _nvs.begin(NVS_NAMESPACE, false);
  bool present = _nvs.isKey(key_name);
  String val   = present ? _nvs.getString(key_name, "") : "";
  _nvs.end();
  return present && val.length() > 0;
}

void nvs_clear_api_key(const char *key_name) {
  _nvs.begin(NVS_NAMESPACE, false);
  _nvs.remove(key_name);
  _nvs.end();
  Serial.printf("[NVS] cle effacee : '%s'\n", key_name);
}

void nvs_list_api_keys_json(char *out_json, size_t len) {
  String json = "{";
  for (int i = 0; KNOWN_KEYS[i] != nullptr; i++) {
    bool has = nvs_has_api_key(KNOWN_KEYS[i]);
    if (i > 0) json += ",";
    json += '"';
    json += KNOWN_KEYS[i];
    json += has ? "\":true" : "\":false";
  }
  json += "}";
  strncpy(out_json, json.c_str(), len - 1);
  out_json[len - 1] = '\0';
}
