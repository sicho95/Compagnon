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
#define API_KEY_GROQ        "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"   // console.groq.com
#define API_KEY_GEMINI      "AIzaSyDMBEwJgGppWbTJTFF8_MAWgE7XdxYtB-g"   // aistudio.google.com (TTS)
#define API_KEY_METEO       "7600de4c129fda32c9b2ba08daf3e7a66bb46267d3da08ba0287ce7d4f046748"   // api.meteo-concept.com
#define API_KEY_SERPER      "6fd048bcdbe579b4ec53204dd2fb3be0e578c917"   // serper.dev (recherche web)
#define API_KEY_TWELVEDATA  "efdaf662ac614e4288bea0c5de0bdf77"   // twelvedata.com (bourse)
