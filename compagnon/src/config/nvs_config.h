#pragma once
/**
 * nvs_config.h — Lecture/écriture des clés API depuis la NVS ESP32
 *
 * Toutes les clés sont stockées dans le namespace unique "compagnon".
 * Elles arrivent exclusivement via BLE (cmd: set_api_key) depuis la PWA.
 *
 * Usage :
 *   char key[128];
 *   if (nvs_get_api_key(NVS_KEY_METEO, key, sizeof(key))) { ... }
 *   nvs_set_api_key(NVS_KEY_TWELVEDATA, "xxx");
 *   nvs_has_api_key(NVS_KEY_GROQ);
 *   nvs_list_api_keys_json(out, sizeof(out));
 */

#ifndef NVS_CONFIG_H
#define NVS_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

// ── Namespace unique pour toutes les apps ─────────────────────────────────────
#define NVS_NAMESPACE "compagnon"

// ── Noms de clés ──────────────────────────────────────────────────────────────
#define NVS_KEY_GROQ          "GROQ_API_KEY"
#define NVS_KEY_GEMINI        "GEMINI_API_KEY"
#define NVS_KEY_SERPER        "SERPER_API_KEY"
#define NVS_KEY_OPENROUTER    "OPENROUTER_API_KEY"
#define NVS_KEY_TWELVEDATA    "TWELVE_DATA_API_KEY"
#define NVS_KEY_METEO         "METEO_CONCEPT_API_KEY"
#define NVS_KEY_SPOTIFY_ID    "SPOTIFY_CLIENT_ID"
#define NVS_KEY_SPOTIFY_SEC   "SPOTIFY_CLIENT_SECRET"

/** Lit une clé API depuis la NVS. Retourne true si trouvée et non vide. */
bool nvs_get_api_key(const char *key_name, char *out, size_t out_len);

/** Écrit une clé API en NVS. Retourne true si succès. */
bool nvs_set_api_key(const char *key_name, const char *value);

/** Vérifie si une clé est présente et non vide. */
bool nvs_has_api_key(const char *key_name);

/** Efface une clé. */
void nvs_clear_api_key(const char *key_name);

/** Remplit un buffer JSON { "KEY": true/false, ... } pour cmd:get_api_keys */
void nvs_list_api_keys_json(char *out_json, size_t len);

#endif // NVS_CONFIG_H
