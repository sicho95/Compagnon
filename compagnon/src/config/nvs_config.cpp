/**
 * nvs_config.cpp — Implémentation lecture/écriture clés API en NVS
 * Namespace unique : NVS_NAMESPACE = "compagnon"
 *
 * FIX 2026-05-12 : nvs_list_api_keys_json() renvoie maintenant les noms
 * LONGS PWA (ex: "METEO_CONCEPT_API_KEY") et non plus les noms courts NVS
 * ("meteo_key"). La PWA (key-sync.js) fait la comparaison sur les noms longs
 * → la détection "clé absente" fonctionnait jamais avant ce fix.
 *
 * 2026-05-12 : ajout TUYA_REGION, TUYA_USER_ID, ECOVACS_COUNTRY_CODE,
 *              ECOVACS_DEVICE_ID (commit 28d2c58 settings-store.js).
 */
#include "nvs_config.h"
#include <Arduino.h>
#include <Preferences.h>

static Preferences _nvs;

// Clés NVS courtes (noms réels stockés en flash)
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
  NVS_KEY_TUYA_REGION,
  NVS_KEY_TUYA_USER,
  NVS_KEY_ECOVACS_U,
  NVS_KEY_ECOVACS_P,
  NVS_KEY_ECOVACS_CC,
  NVS_KEY_ECOVACS_DEV,
  nullptr
};

/**
 * PWA_KEY_NAMES[][2] : { nvs_key_court, nom_long_PWA }
 * Doit rester synchronisé avec getAllApiKeys() dans settings-store.js.
 * ⚠ Ordre identique à KNOWN_KEYS[] ci-dessus.
 */
const char * const PWA_KEY_NAMES[][2] = {
  { NVS_KEY_GROQ,        "GROQ_API_KEY"           },
  { NVS_KEY_GEMINI,      "GEMINI_API_KEY"          },
  { NVS_KEY_SERPER,      "SERPER_API_KEY"          },
  { NVS_KEY_OPENROUTER,  "OPENROUTER_API_KEY"      },
  { NVS_KEY_TWELVEDATA,  "TWELVE_DATA_API_KEY"     },
  { NVS_KEY_METEO,       "METEO_CONCEPT_API_KEY"   },
  { NVS_KEY_SPOTIFY_ID,  "SPOTIFY_CLIENT_ID"       },
  { NVS_KEY_SPOTIFY_SEC, "SPOTIFY_CLIENT_SECRET"   },
  { NVS_KEY_TUYA_ID,     "TUYA_CLIENT_ID"          },
  { NVS_KEY_TUYA_SEC,    "TUYA_CLIENT_SECRET"      },
  { NVS_KEY_TUYA_REGION, "TUYA_REGION"             },
  { NVS_KEY_TUYA_USER,   "TUYA_USER_ID"            },
  { NVS_KEY_ECOVACS_U,   "ECOVACS_EMAIL"           },
  { NVS_KEY_ECOVACS_P,   "ECOVACS_PASSWORD"        },
  { NVS_KEY_ECOVACS_CC,  "ECOVACS_COUNTRY_CODE"    },
  { NVS_KEY_ECOVACS_DEV, "ECOVACS_DEVICE_ID"       },
  { nullptr, nullptr }
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

/**
 * nvs_list_api_keys_json — renvoie un objet JSON avec les noms LONGS PWA
 * Ex: {"GROQ_API_KEY":true,"METEO_CONCEPT_API_KEY":false,...}
 * La PWA compare exactement ces noms dans key-sync.js.
 */
void nvs_list_api_keys_json(char *out_json, size_t len) {
  String json = "{";
  for (int i = 0; PWA_KEY_NAMES[i][0] != nullptr; i++) {
    const char *nvs_key = PWA_KEY_NAMES[i][0];
    const char *pwa_key = PWA_KEY_NAMES[i][1];
    bool has = nvs_has_api_key(nvs_key);
    if (i > 0) json += ",";
    json += '"';
    json += pwa_key;   // ← nom long PWA, pas le nom NVS court
    json += has ? "\":true" : "\":false";
  }
  json += "}";
  strncpy(out_json, json.c_str(), len - 1);
  out_json[len - 1] = '\0';
}

// ── Helpers double/String (utilisés par meteo_app, bourse_app, etc.) ─────────
void nvs_set_double(const char *key, double val) {
  _nvs.begin(NVS_NAMESPACE, false);
  _nvs.putDouble(key, val);
  _nvs.end();
}

double nvs_get_double(const char *key, double def) {
  _nvs.begin(NVS_NAMESPACE, true);
  double v = _nvs.getDouble(key, def);
  _nvs.end();
  return v;
}

String nvs_get_str(const char *key, const char *def) {
  _nvs.begin(NVS_NAMESPACE, true);
  String v = _nvs.getString(key, def ? def : "");
  _nvs.end();
  return v;
}
