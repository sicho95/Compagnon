#pragma once
/**
 * nvs_config.h — Lecture/écriture des clés API depuis la NVS ESP32
 *
 * Toutes les clés sont stockées dans le namespace unique "compagnon".
 * Elles arrivent exclusivement via BLE (cmd: set_api_key) depuis la PWA.
 *
 * ⚠ IMPORTANT : la NVS ESP32 limite les noms de clés à 15 caractères max.
 *   Tout nom plus long est silencieusement tronqué/ignoré, ce qui empêche
 *   la lecture. Tous les NVS_KEY_* ci-dessous sont ≤ 15 caractères.
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

#define NVS_NAMESPACE "compagnon"

// ── Noms de clés NVS (≤ 15 caractères, limite stricte ESP32-IDF) ────────────
#define NVS_KEY_GROQ          "groq_key"         //  8 chars
#define NVS_KEY_GEMINI        "gemini_key"        // 10 chars
#define NVS_KEY_SERPER        "serper_key"        // 10 chars
#define NVS_KEY_OPENROUTER    "openrouter_key"    // 14 chars
#define NVS_KEY_TWELVEDATA    "twelvedata_key"    // 14 chars
#define NVS_KEY_METEO         "meteo_key"         //  9 chars
#define NVS_KEY_SPOTIFY_ID    "spotify_id"        // 10 chars
#define NVS_KEY_SPOTIFY_SEC   "spotify_sec"       // 11 chars
// ── Tuya Cloud (domotique Alexa + SmartLife) ─────────────────────────────────
#define NVS_KEY_TUYA_ID       "tuya_client_id"    // 14 chars
#define NVS_KEY_TUYA_SEC      "tuya_client_sec"   // 15 chars
#define NVS_KEY_TUYA_REGION   "tuya_region"       // 11 chars
#define NVS_KEY_TUYA_USER     "tuya_user_id"      // 12 chars
// ── Ecovacs (robot aspirateur) ───────────────────────────────────────────────
#define NVS_KEY_ECOVACS_U     "ecovacs_user"      // 12 chars
#define NVS_KEY_ECOVACS_P     "ecovacs_pass"      // 12 chars
#define NVS_KEY_ECOVACS_CC    "ecovacs_cc"        // 10 chars
#define NVS_KEY_ECOVACS_DEV   "ecovacs_dev"       // 11 chars

/**
 * PWA_KEY_NAMES[][2] — correspondance NVS_KEY → nom long PWA
 * Utilisé par nvs_list_api_keys_json() pour que la réponse get_api_keys
 * utilise les mêmes noms que getAllApiKeys() dans settings-store.js.
 * ⚠ Ordre identique à KNOWN_KEYS[] dans nvs_config.cpp.
 */
extern const char * const PWA_KEY_NAMES[][2];

void nvs_config_init();
bool nvs_get_api_key(const char *key_name, char *out, size_t out_len);
bool nvs_set_api_key(const char *key_name, const char *value);
bool nvs_has_api_key(const char *key_name);
void nvs_clear_api_key(const char *key_name);
void nvs_list_api_keys_json(char *out_json, size_t len);

// Helpers position GPS (utilisés par meteo_app)
void   nvs_set_double(const char *key, double val);
double nvs_get_double(const char *key, double def);
String nvs_get_str(const char *key, const char *def);

#endif // NVS_CONFIG_H
