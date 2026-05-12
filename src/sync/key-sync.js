/**
 * key-sync.js — Synchronisation automatique des clés API PWA → ESP32 via BLE
 *
 * Flux :
 *  1. À la connexion BLE → syncApiKeys() automatique si hub.ble.autoPushKeys
 *  2. ESP32 répond à cmd:'get_api_keys' avec { KEY_NAME: true/false }
 *  3. Clé absente sur ESP32 + présente en PWA → push BLE cmd:'set_api_key'
 *  4. Clé absente des deux côtés → listée dans report.missing
 *  5. Push manuel possible via forceApiKeySync()
 *
 * Clés gérées :
 *   Nestor  : GROQ_API_KEY, GEMINI_API_KEY, SERPER_API_KEY, OPENROUTER_API_KEY
 *   Bourse  : TWELVE_DATA_API_KEY
 *   Météo   : METEO_CONCEPT_API_KEY
 *   Musique : SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET
 *   Tuya    : TUYA_CLIENT_ID, TUYA_CLIENT_SECRET, TUYA_REGION, TUYA_USER_ID
 *   Ecovacs : ECOVACS_EMAIL, ECOVACS_PASSWORD, ECOVACS_COUNTRY_CODE, ECOVACS_DEVICE_ID
 *
 * NOTE: L'ESP32 (nvs_config.cpp) attend le champ "val" (et non "value")
 *       dans la commande set_api_key.
 *       {"cmd":"set_api_key","key":"TUYA_CLIENT_ID","val":"..."}
 */

import { bleWrite, bleRead } from '../bt/ble.js';
import { getAllApiKeys, getHubSettings } from '../core/settings-store.js';

export async function syncApiKeys() {
  const report = { pushed: [], missing: [], ok: [], error: '' };
  const hubCfg = getHubSettings();
  if (!hubCfg.ble.autoPushKeys) { report.error = 'autoPushKeys désactivé'; return report; }

  try {
    await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'get_api_keys' }));
    const raw = await bleRead('AGENT_SYNC');
    const deviceKeys = JSON.parse(raw);
    const pwaKeys = getAllApiKeys();

    for (const [keyId, pwaValue] of Object.entries(pwaKeys)) {
      if (deviceKeys[keyId] === true) { report.ok.push(keyId); continue; }
      if (!pwaValue || !pwaValue.trim()) { report.missing.push(keyId); continue; }
      try {
        // ⚠️ L'ESP32 attend "val", pas "value"
        await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'set_api_key', key: keyId, val: pwaValue.trim() }));
        report.pushed.push(keyId);
        console.info('[key-sync] ✅', keyId);
      } catch (e) { console.warn('[key-sync] ❌', keyId, e.message); }
    }
  } catch (e) { report.error = e.message; }

  return report;
}

export async function forceApiKeySync() {
  const report = { pushed: [], missing: [], ok: [], error: '' };
  const pwaKeys = getAllApiKeys();
  for (const [keyId, pwaValue] of Object.entries(pwaKeys)) {
    if (!pwaValue || !pwaValue.trim()) { report.missing.push(keyId); continue; }
    try {
      // ⚠️ L'ESP32 attend "val", pas "value"
      await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'set_api_key', key: keyId, val: pwaValue.trim() }));
      report.pushed.push(keyId);
    } catch (e) { report.error += keyId + ': ' + e.message + '\n'; }
  }
  return report;
}

export async function fetchDeviceKeyStatus() {
  await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'get_api_keys' }));
  const raw = await bleRead('AGENT_SYNC');
  return JSON.parse(raw);
}
