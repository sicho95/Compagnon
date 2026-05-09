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

// OTA (Over-The-Air)
#define OTA_PASSWORD "nestor_ota"

// CLÉS API — LLM & TTS
#define GROQ_API_KEY "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"
#define GEMINI_API_KEY "AIzaSyDMBEwJgGppWbTJTFF8_MAWgE7XdxYtB-g"

// CLÉS API — RECHERCHE & MÉTÉO
#define SERPER_KEY "6fd048bcdbe579b4ec53204dd2fb3be0e578c917"
#define API_KEY_METEO "7600de4c129fda32c9b2ba08daf3e7a66bb46267d3da08ba0287ce7d4f046748"

// CLÉS API — FINANCIER
#define TWELVE_DATA_KEY "efdaf662ac614e4288bea0c5de0bdf77"
#define FINNHUB_KEY "YOUR_FINNHUB_KEY_HERE"

#endif // __SECRETS_H__
