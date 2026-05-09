#pragma once
/**
 * nvs_config.h — Lecture/écriture des clés API depuis la NVS ESP32
 *
 * Les clés ne sont PLUS dans secrets.h.
 * Elles arrivent exclusivement via BLE (cmd: set_api_key) depuis la PWA
 * et sont stockées en NVS chiffrée (namespace "apikeys").
 *
 * Usage :
 *   char key[128];
 *   if (nvs_get_api_key("GROQ_API_KEY", key, sizeof(key))) { ... }
 *   nvs_set_api_key("GROQ_API_KEY", "sk-...");
 *   nvs_has_api_key("GROQ_API_KEY");  // présence
 *   nvs_list_api_keys(out, maxKeys);   // pour répondre à cmd:get_api_keys
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

// Noms de clés reconnus
#define NVS_KEY_GROQ          "GROQ_API_KEY"
#define NVS_KEY_GEMINI        "GEMINI_API_KEY"
#define NVS_KEY_SERPER        "SERPER_API_KEY"
#define NVS_KEY_OPENROUTER    "OPENROUTER_API_KEY"
#define NVS_KEY_TWELVEDATA    "TWELVE_DATA_API_KEY"
#define NVS_KEY_METEO         "METEO_CONCEPT_API_KEY"
#define NVS_KEY_SPOTIFY_ID    "SPOTIFY_CLIENT_ID"
#define NVS_KEY_SPOTIFY_SEC   "SPOTIFY_CLIENT_SECRET"

#define NVS_NAMESPACE "apikeys"

/** Lit une clé API depuis la NVS. Retourne true si trouvée et non vide. */
bool nvs_get_api_key(const char *key_name, char *out, size_t out_len);

/** Écrit une clé API en NVS. Retourne true si succès. */
bool nvs_set_api_key(const char *key_name, const char *value);

/** Vérifie si une clé est présente et non vide. */
bool nvs_has_api_key(const char *key_name);

/** Efface une clé. */
void nvs_clear_api_key(const char *key_name);

/**
 * Remplit un tableau de { name, present } pour répondre à cmd:get_api_keys
 * out_json doit être un buffer JSON de taille suffisante (~512 octets)
 */
void nvs_list_api_keys_json(char *out_json, size_t len);

#endif // NVS_CONFIG_H
