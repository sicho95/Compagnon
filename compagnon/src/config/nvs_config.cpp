/**
 * nvs_config.cpp — Implémentation lecture/écriture clés API en NVS
 * Namespace unique : NVS_NAMESPACE = "compagnon" (défini dans nvs_config.h)
 *
 * ⚠ Toutes les clés NVS doivent faire ≤ 15 caractères (limite ESP32-IDF).
 *   Les noms courts sont définis dans nvs_config.h (NVS_KEY_*).
 *
 * IMPORTANT : nvs_has_api_key() et nvs_get_api_key() ouvrent le namespace en
 * mode lecture-écriture (false) pour le créer automatiquement s'il n'existe
 * pas encore. Ouvrir en read-only (true) sur un namespace inexistant retourne
 * l'erreur NVS_NOT_FOUND visible dans les logs Preferences.cpp:47.
 */
#include "nvs_config.h"
#include <Arduino.h>
#include <Preferences.h>

static Preferences _nvs;

// Clés reconnues pour nvs_list_api_keys_json
static const char *KNOWN_KEYS[] = {
  NVS_KEY_GROQ,
  NVS_KEY_GEMINI,
  NVS_KEY_SERPER,
  NVS_KEY_OPENROUTER,
  NVS_KEY_TWELVEDATA,
  NVS_KEY_METEO,
  NVS_KEY_SPOTIFY_ID,
  NVS_KEY_SPOTIFY_SEC,
  nullptr
};

bool nvs_get_api_key(const char *key_name, char *out, size_t out_len) {
  // Ouvrir en read-write (false) : crée le namespace s'il n'existe pas encore
  // et évite l'erreur NOT_FOUND de Preferences.cpp:47
  _nvs.begin(NVS_NAMESPACE, false);
  if (!_nvs.isKey(key_name)) {
    _nvs.end();
    Serial.printf("[NVS] clé absente : '%s' (namespace: %s)\n", key_name, NVS_NAMESPACE);
    return false;
  }
  String val = _nvs.getString(key_name, "");
  _nvs.end();
  if (val.length() == 0) {
    Serial.printf("[NVS] clé vide   : '%s'\n", key_name);
    return false;
  }
  strncpy(out, val.c_str(), out_len - 1);
  out[out_len - 1] = '\0';
  Serial.printf("[NVS] clé lue    : '%s' (%d chars)\n", key_name, (int)val.length());
  return true;
}

bool nvs_set_api_key(const char *key_name, const char *value) {
  if (!value || value[0] == '\0') {
    Serial.printf("[NVS] set_api_key : valeur vide pour '%s' — ignoré\n", key_name);
    return false;
  }
  _nvs.begin(NVS_NAMESPACE, false);
  bool ok = _nvs.putString(key_name, value);
  _nvs.end();
  if (ok)
    Serial.printf("[NVS] clé stockée : '%s' (%d chars) namespace: %s\n",
                  key_name, (int)strlen(value), NVS_NAMESPACE);
  else
    Serial.printf("[NVS] ERREUR stockage : '%s'\n", key_name);
  return ok;
}

bool nvs_has_api_key(const char *key_name) {
  // Ouvrir en read-write (false) pour créer le namespace si absent
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
  Serial.printf("[NVS] clé effacée : '%s'\n", key_name);
}

void nvs_list_api_keys_json(char *out_json, size_t len) {
  // Génère : {"groq_key":true,"gemini_key":false,...}
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
