/**
 * key-sync.js — Synchronisation automatique des clés API PWA → ESP32 via BLE
 *
 * Flux :
 *  1. À la connexion BLE → syncApiKeys() automatique si hub.ble.autoPushKeys
 *  2. ESP32 répond à cmd:'get_api_keys' via NOTIFICATION (pas readValue)
 *     avec { KEY_NAME: true/false }
 *  3. Clé absente sur ESP32 + présente en PWA → push BLE cmd:'set_api_key'
 *  4. Clé absente des deux côtés → listée dans report.missing
 *  5. Push manuel possible via forceApiKeySync()
 *
 * CORRECTIF BUG : bleRead() lit la valeur statique de la caractéristique
 * AVANT que l'ESP32 ne l'ait mise à jour. L'ESP32 répond via NOTIFICATION.
 * On utilise donc bleSubscribeOnce() — subscribe + Promise + timeout 5s.
 */

import { bleWrite, bleSubscribe } from '../bt/ble.js';
import { getAllApiKeys, getHubSettings } from '../core/settings-store.js';

/**
 * Attend une seule notification sur charKey et résout avec la valeur décodée.
 * Timeout configurable (défaut 5 s).
 */
function bleSubscribeOnce(charKey, timeoutMs = 5000) {
  return new Promise((resolve, reject) => {
    let settled = false;
    const timer = setTimeout(() => {
      if (!settled) { settled = true; reject(new Error(`BLE timeout (${charKey})`)); }
    }, timeoutMs);

    // bleSubscribe enregistre un listener permanent — on wrappe pour n'écouter qu'une fois
    bleSubscribe(charKey, (raw) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      resolve(raw);
    }).catch(err => {
      if (!settled) { settled = true; clearTimeout(timer); reject(err); }
    });
  });
}

export async function syncApiKeys() {
  const report = { pushed: [], missing: [], ok: [], error: '' };
  const hubCfg = getHubSettings();
  if (!hubCfg.ble.autoPushKeys) { report.error = 'autoPushKeys désactivé'; return report; }

  try {
    // Prépare l'écoute AVANT d'envoyer la commande (évite race condition)
    const responsePromise = bleSubscribeOnce('AGENT_SYNC', 5000);
    await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'get_api_keys' }));
    const raw = await responsePromise;

    let deviceKeys = {};
    try { deviceKeys = JSON.parse(raw); } catch { /* réponse non-JSON ignorée */ }

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
  const responsePromise = bleSubscribeOnce('AGENT_SYNC', 5000);
  await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'get_api_keys' }));
  const raw = await responsePromise;
  return JSON.parse(raw);
}
