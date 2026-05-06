import { defaultAgents } from '../core/default-agents.js';

const DB_NAME = 'nestor-agents-v1';
const STORE_AGENTS = 'agents';

function openDb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 1);
    req.onupgradeneeded = (event) => {
      const db = event.target.result;
      if (!db.objectStoreNames.contains(STORE_AGENTS)) {
        db.createObjectStore(STORE_AGENTS, { keyPath: 'id' });
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror = () => reject(req.error);
  });
}

async function getAllRaw() {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_AGENTS, 'readonly');
    const req = tx.objectStore(STORE_AGENTS).getAll();
    req.onsuccess = () => resolve(req.result || []);
    req.onerror = () => reject(req.error);
  });
}

// Charge les agents. Si la base est vide, seed les agents par défaut.
export async function loadAgents() {
  const existing = await getAllRaw();
  if (existing.length > 0) return existing;
  const seeded = defaultAgents();
  for (const agent of seeded) await saveAgentRaw(agent);
  return seeded;
}

async function saveAgentRaw(agent) {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_AGENTS, 'readwrite');
    const req = tx.objectStore(STORE_AGENTS).put(agent);
    req.onsuccess = () => resolve(agent);
    req.onerror = () => reject(req.error);
  });
}

export async function saveAgent(agent) {
  agent.updatedAt = new Date().toISOString();
  return saveAgentRaw(agent);
}

export async function deleteAgent(id) {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx = db.transaction(STORE_AGENTS, 'readwrite');
    const req = tx.objectStore(STORE_AGENTS).delete(id);
    req.onsuccess = () => resolve();
    req.onerror = () => reject(req.error);
  });
}

export function exportAgentsJson(agents) {
  return JSON.stringify({ format: 'nestor-agents-1', exportedAt: new Date().toISOString(), agents }, null, 2);
}

export function downloadText(filename, text) {
  const blob = new Blob([text], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = filename;
  document.body.appendChild(a); a.click();
  document.body.removeChild(a); URL.revokeObjectURL(url);
}

export async function importAgentsJson(json, gardenerMergeFn) {
  const parsed = JSON.parse(json);
  if (parsed.format !== 'nestor-agents-1') throw new Error('Format inconnu');
  const existing = await getAllRaw();
  const merged = await gardenerMergeFn(existing, parsed.agents || []);
  for (const agent of merged) await saveAgent(agent);
  return merged;
}

// localStorage sécurisé (PWA iOS peut bloquer)
export function lsGet(key) { try { return localStorage.getItem(key) || ''; } catch { return ''; } }
export function lsSet(key, val) { try { localStorage.setItem(key, val); } catch { console.warn('[Nestor] localStorage indisponible'); } }

// ─── Historique de conversation par agent (localStorage) ─────────────────────
const HIST_PREFIX = 'NESTOR_HIST_';
const HIST_MAX_MESSAGES = 100;

export function saveChatHistory(agentId, history) {
  // Ne persiste pas le message system (index 0)
  const toSave = history.filter(m => m.role !== 'system').slice(-HIST_MAX_MESSAGES);
  lsSet(HIST_PREFIX + agentId, JSON.stringify(toSave));
}

export function loadChatHistory(agentId) {
  try {
    const raw = lsGet(HIST_PREFIX + agentId);
    return raw ? JSON.parse(raw) : [];
  } catch { return []; }
}

export function clearChatHistory(agentId) {
  try { localStorage.removeItem(HIST_PREFIX + agentId); } catch {}
}
