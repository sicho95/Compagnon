import { defaultAgents } from '../core/default-agents.js';

const DB_NAME    = 'nestor-agents-v1';
const STORE_AGENTS   = 'agents';
const STORE_SETTINGS = 'settings';

// ─── IndexedDB open (agents + settings stores) ───────────────────────────────
function openDb() {
  return new Promise((resolve, reject) => {
    const req = indexedDB.open(DB_NAME, 2); // version 2 : ajout store settings
    req.onupgradeneeded = (event) => {
      const db = event.target.result;
      if (!db.objectStoreNames.contains(STORE_AGENTS)) {
        db.createObjectStore(STORE_AGENTS, { keyPath: 'id' });
      }
      if (!db.objectStoreNames.contains(STORE_SETTINGS)) {
        db.createObjectStore(STORE_SETTINGS); // keyPath : clé libre
      }
    };
    req.onsuccess = () => resolve(req.result);
    req.onerror   = () => reject(req.error);
  });
}

// ─── Agents ──────────────────────────────────────────────────────────────────
async function getAllRaw() {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx  = db.transaction(STORE_AGENTS, 'readonly');
    const req = tx.objectStore(STORE_AGENTS).getAll();
    req.onsuccess = () => resolve(req.result || []);
    req.onerror   = () => reject(req.error);
  });
}

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
    const tx  = db.transaction(STORE_AGENTS, 'readwrite');
    const req = tx.objectStore(STORE_AGENTS).put(agent);
    req.onsuccess = () => resolve(agent);
    req.onerror   = () => reject(req.error);
  });
}

export async function saveAgent(agent) {
  agent.updatedAt = new Date().toISOString();
  return saveAgentRaw(agent);
}

export async function deleteAgent(id) {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx  = db.transaction(STORE_AGENTS, 'readwrite');
    const req = tx.objectStore(STORE_AGENTS).delete(id);
    req.onsuccess = () => resolve();
    req.onerror   = () => reject(req.error);
  });
}

export function exportAgentsJson(agents) {
  return JSON.stringify({ format: 'nestor-agents-1', exportedAt: new Date().toISOString(), agents }, null, 2);
}

export function downloadText(text, filename, mime) {
  const blob = new Blob([text], { type: mime || 'application/json' });
  const url  = URL.createObjectURL(blob);
  const a    = document.createElement('a');
  a.href = url; a.download = filename;
  document.body.appendChild(a); a.click();
  document.body.removeChild(a); URL.revokeObjectURL(url);
}

export async function importAgentsJson(json, currentAgents) {
  const parsed = JSON.parse(json);
  if (parsed.format !== 'nestor-agents-1') throw new Error('Format inconnu');
  const incoming = parsed.agents || [];
  const merged = [...currentAgents];
  for (const a of incoming) {
    const idx = merged.findIndex(x => x.id === a.id);
    if (idx >= 0) merged[idx] = a; else merged.push(a);
  }
  for (const agent of merged) await saveAgent(agent);
  return merged;
}

// ─── Settings via IndexedDB (remplace localStorage) ──────────────────────────
// Cache mémoire pour accès synchrone après init
const _settingsCache = {};
let   _dbReady = false;

/**
 * Indique si le cache IDB est prêt. Utile pour les diagnostics.
 */
export function isDbReady() { return _dbReady; }

/**
 * À appeler une seule fois au boot (dans main()) avant toute lecture de settings.
 * Pré-charge tous les namespaces dans _settingsCache.
 */
export async function initSettingsStore() {
  const db = await openDb();
  return new Promise((resolve, reject) => {
    const tx  = db.transaction(STORE_SETTINGS, 'readonly');
    const req = tx.objectStore(STORE_SETTINGS).openCursor();
    req.onsuccess = (e) => {
      const cursor = e.target.result;
      if (cursor) { _settingsCache[cursor.key] = cursor.value; cursor.continue(); }
      else { _dbReady = true; resolve(); }
    };
    req.onerror = () => reject(req.error);
  });
}

export function lsGet(key) {
  // ── Cas 1 : IDB prêt → source de vérité unique
  if (_dbReady) return _settingsCache[key] ?? '';

  // ── Cas 2 : IDB pas encore prêt (appel synchrone avant await initSettingsStore())
  // Priorité 1 : cache mémoire déjà partiellement peuplé (ex : lsSet appelé avant init)
  if (key in _settingsCache) return _settingsCache[key] ?? '';

  // Priorité 2 : fallback localStorage — ne contient les clés que si elles ont été
  // écrites AVANT la migration vers IDB. Pour les nouveaux utilisateurs ce retour
  // sera '' mais c'est acceptable : le 401 Groq ne se produit que si l'utilisateur
  // appelle callLLM avant la fin de initSettingsStore(), ce qui est évité par
  // l'ordre d'appel dans app.js (initSettingsStore → initBackends → render).
  try { return localStorage.getItem(key) || ''; } catch { return ''; }
}

export function lsSet(key, val) {
  _settingsCache[key] = val;
  // Écriture async IDB (fire & forget)
  openDb().then(db => {
    const tx  = db.transaction(STORE_SETTINGS, 'readwrite');
    tx.objectStore(STORE_SETTINGS).put(val, key);
  }).catch(e => console.warn('[Nestor] IDB lsSet:', e));
  // Écriture localStorage en parallèle pour migration douce
  try { localStorage.setItem(key, val); } catch { /* iOS strict mode — ignoré */ }
}

// ─── Historique de conversation par agent ────────────────────────────────────
const HIST_PREFIX     = 'NESTOR_HIST_';
const HIST_MAX_MESSAGES = 100;

export function saveChatHistory(agentId, history) {
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
  delete _settingsCache[HIST_PREFIX + agentId];
  openDb().then(db => {
    const tx = db.transaction(STORE_SETTINGS, 'readwrite');
    tx.objectStore(STORE_SETTINGS).delete(HIST_PREFIX + agentId);
  }).catch(() => {});
  try { localStorage.removeItem(HIST_PREFIX + agentId); } catch {}
}
