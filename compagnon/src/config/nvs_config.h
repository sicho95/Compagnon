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

// ── Noms de clés NVS (≤ 15 caractères, limite stricte ESP32-IDF) ─────────────
// Les noms courts sont utilisés EN NVS UNIQUEMENT.
// La PWA envoie les noms longs (ex: "GROQ_API_KEY") dans la commande BLE
// set_api_key → le handler BLE doit mapper vers ces noms courts via ble_mgr.
#define NVS_KEY_GROQ          "groq_key"         //  8 chars
#define NVS_KEY_GEMINI        "gemini_key"        // 10 chars
#define NVS_KEY_SERPER        "serper_key"        // 10 chars
#define NVS_KEY_OPENROUTER    "openrouter_key"    // 14 chars
#define NVS_KEY_TWELVEDATA    "twelvedata_key"    // 14 chars
#define NVS_KEY_METEO         "meteo_key"         //  9 chars
#define NVS_KEY_SPOTIFY_ID    "spotify_id"        // 10 chars
#define NVS_KEY_SPOTIFY_SEC   "spotify_sec"       // 11 chars

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
