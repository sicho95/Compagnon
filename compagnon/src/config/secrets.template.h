/**
 * SECRETS TEMPLATE — ESP32 Compagnon
 * Copier en secrets.h + remplir les valeurs réelles
 * JAMAIS commit secrets.h (listé dans .gitignore)
 */

#ifndef __SECRETS_H__
#define __SECRETS_H__

// WIFI CAPTIF
#define WIFI_AP_SSID "Compagnon_Setup"
#define WIFI_AP_PSK "compagnon"

// Clés API — peuvent aussi être saisies via le portail captif
// et seront stockées en NVS chiffré sur l'ESP32
#define API_KEY_GROQ        ""   // console.groq.com
#define API_KEY_GEMINI      ""   // aistudio.google.com (TTS)
#define API_KEY_METEO       ""   // api.meteo-concept.com
#define API_KEY_SERPER      ""   // serper.dev (recherche web)
#define API_KEY_TWELVEDATA  ""   // twelvedata.com (bourse)

// Spotify Connect — créer une app sur developer.spotify.com
// Redirect URI à déclarer : http://localhost / ou l'URL de la PWA
#define SPOTIFY_CLIENT_ID     ""
#define SPOTIFY_CLIENT_SECRET ""
