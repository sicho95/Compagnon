#pragma once
// ─────────────────────────────────────────────────────────────────────
// TEMPLATE — copier en secrets.h et renseigner vos valeurs
// secrets.h est dans .gitignore : il ne sera JAMAIS commité
// ─────────────────────────────────────────────────────────────────────

// Mot de passe du point d'accès captif de configuration WiFi
// (affiché à l'écran lors du premier démarrage)
#define WIFI_AP_PSK       "compagnon"

// Mot de passe OTA (mise à jour firmware sans câble USB)
#define OTA_PASSWORD      "nestor_ota"

// Clés API — peuvent aussi être saisies via le portail captif
// et seront stockées en NVS chiffré sur l'ESP32
#define API_KEY_GROQ        ""   // console.groq.com
#define API_KEY_GEMINI      ""   // aistudio.google.com (TTS)
#define API_KEY_METEO       ""   // api.meteo-concept.com
#define API_KEY_SERPER      ""   // serper.dev (recherche web)
#define API_KEY_TWELVEDATA  ""   // twelvedata.com (bourse)
