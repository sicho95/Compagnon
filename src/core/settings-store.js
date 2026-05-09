/**
 * settings-store.js — Stockage centralisé et cloisonné des paramètres par app
 *
 * Architecture :
 *   hub      → paramètres globaux : nom, langue, BLE, NTP, display, apps activées
 *   nestor   → clés LLM (Groq, Gemini TTS, Serper, OpenRouter), proxy
 *   bourse   → clé TwelveData, tickers
 *   meteo    → clé Météo-Concept, position par défaut
 *   musique  → Spotify client ID/secret
 *   battery  → profil PMIC AT103030
 *
 * Stockage : IndexedDB via lsGet/lsSet (cache mémoire synchrone après init)
 * Jamais de fichier secrets.js — les clés viennent de l'utilisateur via l'UI
 * ou sont synchronisées automatiquement depuis l'ESP32 via BLE.
 */

import { lsGet, lsSet } from '../storage/agents-db.js';

const DEFAULTS = {
  hub: {
    name: 'Nestor',
    language: 'fr',
    ntpServer: 'pool.ntp.org',
    timezone: 'Europe/Paris',
    display: { brightness: 80, sleepAfterMs: 30000, theme: 'dark' },
    ble: { autoSyncAgents: true, relayLlmOnNoWifi: true, autoPushKeys: true },
    apps: {
      nestor:  { enabled: true },
      radar:   { enabled: true },
      meteo:   { enabled: true },
      bourse:  { enabled: true },
      musique: { enabled: true },
    },
  },

  nestor: {
    provider: 'groq',
    model: 'llama3-70b-8192',
    apiKey: '',
    geminiApiKey: '',
    serperApiKey: '',
    openrouterApiKey: '',
    proxyUrl: 'https://proxy.sicho95.workers.dev',
  },

  bourse: {
    twelveDataApiKey: '',
    tickers: ['AAPL', 'BTC/USD', 'MC.PA', 'CAC:INDX'],
    refreshIntervalMin: 5,
  },

  meteo: {
    meteoConcept: '',
    defaultLat: 48.8566,
    defaultLon: 2.3522,
    defaultCity: 'Paris',
  },

  musique: {
    spotifyClientId: '',
    spotifyClientSecret: '',
    spotifyRedirectUri: '',
  },

  battery: {
    model: 'AT103030',
    capacityMah: 1000,
    chargeVoltageMv: 4200,
    chargeCurrentMa: 500,
    terminationCurrentMa: 50,
    alertLowMv: 3300,
    alertCriticalMv: 3000,
    gaugeEnabled: true,
    displayMode: 'percent',
  },
};

function nsKey(ns) { return 'nestor_settings_' + ns; }

function deepMerge(base, over) {
  if (!over || typeof over !== 'object') return base;
  const r = { ...base };
  for (const k of Object.keys(over)) {
    r[k] = (over[k] && typeof over[k] === 'object' && !Array.isArray(over[k]))
      ? deepMerge(base[k] || {}, over[k])
      : over[k];
  }
  return r;
}

export function getSettings(ns) {
  if (!DEFAULTS[ns]) throw new Error('[settings] namespace inconnu : ' + ns);
  try {
    const raw = lsGet(nsKey(ns));
    return raw ? deepMerge(DEFAULTS[ns], JSON.parse(raw)) : { ...DEFAULTS[ns] };
  } catch {
    return { ...DEFAULTS[ns] };
  }
}

export function setSettings(ns, partial) {
  if (!DEFAULTS[ns]) throw new Error('[settings] namespace inconnu : ' + ns);
  const current = getSettings(ns);
  const updated = deepMerge(current, partial);
  lsSet(nsKey(ns), JSON.stringify(updated));
  return updated;
}

export function resetSettings(ns) {
  lsSet(nsKey(ns), JSON.stringify(DEFAULTS[ns]));
  return { ...DEFAULTS[ns] };
}

export const NAMESPACES = Object.keys(DEFAULTS);

export const getHubSettings     = () => getSettings('hub');
export const getNestorSettings  = () => getSettings('nestor');
export const getBourseSettings  = () => getSettings('bourse');
export const getMeteoSettings   = () => getSettings('meteo');
export const getMusiqueSettings = () => getSettings('musique');
export const getBatterySettings = () => getSettings('battery');

export const setHubSettings     = (p) => setSettings('hub', p);
export const setNestorSettings  = (p) => setSettings('nestor', p);
export const setBourseSettings  = (p) => setSettings('bourse', p);
export const setMeteoSettings   = (p) => setSettings('meteo', p);
export const setMusiqueSettings = (p) => setSettings('musique', p);
export const setBatterySettings = (p) => setSettings('battery', p);

export function getAllApiKeys() {
  const n  = getNestorSettings();
  const b  = getBourseSettings();
  const m  = getMeteoSettings();
  const mu = getMusiqueSettings();
  return {
    GROQ_API_KEY:          n.apiKey,
    GEMINI_API_KEY:        n.geminiApiKey,
    SERPER_API_KEY:        n.serperApiKey,
    OPENROUTER_API_KEY:    n.openrouterApiKey,
    TWELVE_DATA_API_KEY:   b.twelveDataApiKey,
    METEO_CONCEPT_API_KEY: m.meteoConcept,
    SPOTIFY_CLIENT_ID:     mu.spotifyClientId,
    SPOTIFY_CLIENT_SECRET: mu.spotifyClientSecret,
  };
}

export function setApiKey(keyId, value) {
  switch (keyId) {
    case 'GROQ_API_KEY':          return setSettings('nestor', { apiKey: value });
    case 'GEMINI_API_KEY':        return setSettings('nestor', { geminiApiKey: value });
    case 'SERPER_API_KEY':        return setSettings('nestor', { serperApiKey: value });
    case 'OPENROUTER_API_KEY':    return setSettings('nestor', { openrouterApiKey: value });
    case 'TWELVE_DATA_API_KEY':   return setSettings('bourse', { twelveDataApiKey: value });
    case 'METEO_CONCEPT_API_KEY': return setSettings('meteo',  { meteoConcept: value });
    case 'SPOTIFY_CLIENT_ID':     return setSettings('musique', { spotifyClientId: value });
    case 'SPOTIFY_CLIENT_SECRET': return setSettings('musique', { spotifyClientSecret: value });
    default: console.warn('[settings] clé inconnue :', keyId);
  }
}
