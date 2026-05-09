/**
 * ble_protocol.js — Protocole JSON haut niveau BLE Nestor
 */
import { bleWrite, bleRead, bleSubscribe } from './ble.js';

export async function bleRequestWifiScan() {
  // Déclencher le scan en envoyant l'octet 0x01
  await bleWrite('WIFI_SCAN', new Uint8Array([0x01]));
  return new Promise((resolve, reject) => {
    let done = false;
    const t = setTimeout(() => { done = true; resolve([]); }, 10000);
    bleSubscribe('WIFI_SCAN', raw => {
      if (done) return;
      try {
        const d = JSON.parse(raw);
        if (Array.isArray(d)) {
          done = true;
          clearTimeout(t);
          // Normaliser le format compact {s, r} en {ssid, rssi} pour l'interface
          resolve(d.map(n => ({ ssid: n.s, rssi: n.r })));
        }
      } catch {}
    }).catch(e => { if (!done) { done = true; clearTimeout(t); reject(e); } });
  });
}

export async function bleProvisionWifi(ssid, password) {
  await bleWrite('WIFI_PROVISION', { ssid, password });
  // Note : on n'ouvre PAS une nouvelle subscription sur DEVICE_STATUS ici —
  // celle-ci est déjà active depuis bleConnect() via startNotifications().
  // On expose un timeout propre ; la mise à jour du statut arrivera via
  // onStatusChange -> ble_status.js -> updateDeviceStatus.
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => reject(new Error('Timeout provision WiFi')), 15000);
    // Écouter les changements de deviceStatus via ble_status
    import('./ble_status.js').then(({ subscribeBleStatus }) => {
      const unsub = subscribeBleStatus(s => {
        if (s.wifi === 'connected') { clearTimeout(t); unsub(); resolve(s); }
        if (s.wifi === 'failed')    { clearTimeout(t); unsub(); reject(new Error('WiFi échoué: ' + (s.reason || '?'))); }
      });
    }).catch(() => reject(new Error('ble_status non disponible')));
  });
}

export async function bleReadAgents() {
  await bleWrite('AGENT_SYNC', JSON.stringify({ cmd: 'get_agents' }));
  const raw = await bleRead('AGENT_SYNC');
  return JSON.parse(raw).agents || [];
}

export async function blePushAgents(agents) {
  await bleWrite('AGENT_SYNC', { cmd: 'push', agents });
}

export async function bleSyncAgents(localAgents) {
  const remote = await bleReadAgents();
  const map = {};
  for (const a of localAgents) map[a.id] = { ...a };
  for (const a of remote) {
    if (!map[a.id]) { map[a.id] = { ...a }; }
    else {
      const lt = new Date(map[a.id].updatedAt || 0).getTime();
      const rt = new Date(a.updatedAt || 0).getTime();
      if (rt > lt) map[a.id] = { ...a };
    }
  }
  const merged = Object.values(map);
  await blePushAgents(merged);
  return merged;
}

export async function bleSendText(text) {
  await bleWrite('TEXT_INPUT', { text });
}

export function setupLlmRelay(llmCallFn) {
  // bleSubscribe retourne une Promise — on l'attrape pour éviter une unhandled rejection
  bleSubscribe('LLM_RELAY', async raw => {
    try {
      const req = JSON.parse(raw);
      if (req.cmd !== 'request') return;
      const content = await llmCallFn(req.messages, req.model, req.agentId);
      await bleWrite('LLM_RELAY', { cmd: 'response', reqId: req.reqId, content });
    } catch (e) {
      await bleWrite('LLM_RELAY', { cmd: 'error', message: e.message }).catch(() => {});
    }
  }).catch(e => console.warn('[BLE] setupLlmRelay subscribe:', e.message));
}

export async function bleGetDeviceStatus() {
  return JSON.parse(await bleRead('DEVICE_STATUS'));
}
