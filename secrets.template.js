/**
 * SECRETS TEMPLATE — Copier en secrets.js + remplir les valeurs réelles
 * JAMAIS commit secrets.js (listé dans .gitignore)
 *
 * source des clés : iCloud Documents/Arduino/libraries/clé (texte)
 */

// Clés API — Backends LLM
export const GROQ_API_KEY = 'gsk_YOUR_GROQ_KEY_HERE';
export const OPENROUTER_API_KEY = 'sk_YOUR_OPENROUTER_KEY_HERE';
export const GEMINI_API_KEY = 'YOUR_GEMINI_API_KEY_HERE';

// Clés API — Recherche Web
export const SERPER_KEY = 'YOUR_SERPER_KEY_HERE';

// Clés API — Financier & Météo
export const TWELVE_DATA_KEY = 'YOUR_TWELVE_DATA_KEY_HERE';
export const FINNHUB_KEY = 'YOUR_FINNHUB_KEY_HERE';
export const METEO_API_KEY = 'YOUR_METEO_API_KEY_HERE';

// Proxy CORS (optionnel)
export const SEARCH_PROXY_URL = 'https://proxy.sicho95.workers.dev/';
