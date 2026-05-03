import { lsGet } from '../storage/agents-db.js';

let activeBackends = {};
let _groqDynamicModels = null; // Cache des modèles Groq fetchés dynamiquement

// Backends statiques de base (utilisés si le fetch dynamique Groq échoue)
// La liste Groq sera enrichie dynamiquement depuis l'API Groq
const DEFAULT_BACKENDS = {
  'groq-llama': {
    label: 'Groq — llama-3.3-70b-versatile',
    type: 'openai-compatible',
    baseUrl: 'https://api.groq.com/openai/v1',
    chatPath: '/chat/completions',
    model: 'llama-3.3-70b-versatile',
    requiresApiKey: true,
    envKey: 'GROQ_API_KEY',
    groqDynamic: true,
  },
  'puter-qwen': {
    label: 'Puter Qwen (secours sans clé)',
    type: 'puter-qwen',
    model: 'qwen/qwen-plus',
    requiresApiKey: false,
  },
  'perplexity-sonar': {
    label: 'Perplexity Sonar (web)',
    type: 'openai-compatible',
    baseUrl: 'https://api.perplexity.ai',
    chatPath: '/chat/completions',
    model: 'sonar',
    requiresApiKey: true,
    envKey: 'PERPLEXITY_API_KEY',
  },
};

// ─── Fetch dynamique des modèles Groq ────────────────────────────────────────
// Enrichit la liste de backends avec TOUS les modèles Groq disponibles
// (llama, mixtral, gemma, qwen, deepseek, kimi, etc.)
// Une seule clé GROQ_API_KEY est utilisée pour tous.

export async function loadGroqModels() {
  const apiKey = (lsGet('GROQ_API_KEY') || '').trim();
  if (!apiKey) return; // Pas de clé = on reste sur les backends statiques
  if (_groqDynamicModels) return; // Déjà chargé

  try {
    const res = await fetch('https://api.groq.com/openai/v1/models', {
      headers: { Authorization: 'Bearer ' + apiKey },
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    const models = (data.data || [])
      .filter(m => m.id && !m.id.includes('whisper') && !m.id.includes('distil')) // Exclure STT
      .sort((a, b) => a.id.localeCompare(b.id));

    if (models.length === 0) return;

    _groqDynamicModels = models;

    // Remplacer/enrichir les backends Groq dans activeBackends
    // Supprimer les anciens backends groq-dynamic-*
    Object.keys(activeBackends)
      .filter(k => k.startsWith('groq-dynamic-'))
      .forEach(k => delete activeBackends[k]);

    // Ajouter un backend par modèle Groq
    for (const m of models) {
      const id = 'groq-dynamic-' + m.id.replace(/[^a-z0-9-]/gi, '-');
      activeBackends[id] = {
        label: 'Groq — ' + m.id,
        type: 'openai-compatible',
        baseUrl: 'https://api.groq.com/openai/v1',
        chatPath: '/chat/completions',
        model: m.id,
        requiresApiKey: true,
        envKey: 'GROQ_API_KEY',
        groqDynamic: true,
      };
    }

    // Conserver groq-llama pointant sur le modèle par défaut
    activeBackends['groq-llama'].model = 'llama-3.3-70b-versatile';
    activeBackends['groq-llama'].label = 'Groq — llama-3.3-70b-versatile (défaut)';

  } catch (e) {
    console.warn('[Nestor/backends] Chargement modèles Groq échoué :', e.message);
  }
}

// ─── Init ─────────────────────────────────────────────────────────────────────

export async function initBackends() {
  try {
    const res = await fetch('./src/api/backends.json');
    if (!res.ok) throw new Error('backends.json absent');
    activeBackends = await res.json();
  } catch (_) {
    activeBackends = { ...DEFAULT_BACKENDS };
  }
  // Tentative de chargement dynamique des modèles Groq
  await loadGroqModels();
}

export function listBackends() {
  return Object.entries(activeBackends).map(([id, cfg]) => ({ id, ...cfg }));
}

// ─── Puter loader ─────────────────────────────────────────────────────────────

function waitForPuter(timeout = 8000) {
  return new Promise((resolve, reject) => {
    if (window.puter && window.puter.ai) return resolve(window.puter);
    const start = Date.now();
    const check = setInterval(() => {
      if (window.puter && window.puter.ai) { clearInterval(check); resolve(window.puter); }
      else if (Date.now() - start > timeout) { clearInterval(check); reject(new Error('Puter.js non chargé.')); }
    }, 100);
  });
}

// ─── callLLM ──────────────────────────────────────────────────────────────────

export async function callLLM(backendId, { messages, agentConfig }) {
  const cfg = activeBackends[backendId] || activeBackends['groq-llama'];
  if (!cfg) throw new Error('Aucun backend disponible.');

  if (cfg.type === 'puter-qwen' || cfg.type === 'puter-gpt4o') {
    const puter = await waitForPuter();
    const model = cfg.model || (cfg.type === 'puter-gpt4o' ? 'gpt-4o' : 'qwen/qwen-plus');
    const res = await puter.ai.chat(messages, { model });
    const content = typeof res === 'string' ? res
      : res?.message?.content || res?.content || res?.toString() || '';
    return { message: { role: 'assistant', content } };
  }

  if (cfg.type === 'openai-compatible') {
    // Une seule clé GROQ_API_KEY pour tous les backends Groq (groqDynamic ou pas)
    const apiKey = cfg.envKey ? (lsGet(cfg.envKey) || '') : '';
    if (cfg.requiresApiKey && !apiKey) {
      throw new Error('Clé API manquante pour "' + cfg.label + '". Va dans Réglages.');
    }
    const res = await fetch(cfg.baseUrl + cfg.chatPath, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        ...(apiKey ? { Authorization: 'Bearer ' + apiKey } : {}),
      },
      body: JSON.stringify({ model: cfg.model, messages }),
    });
    if (!res.ok) {
      const errText = await res.text().catch(() => '');
      throw new Error('Erreur LLM ' + res.status + (errText ? ' : ' + errText.slice(0, 120) : ''));
    }
    const data = await res.json();
    return data.choices?.[0] || { message: { role: 'assistant', content: '(réponse vide)' } };
  }

  throw new Error('Type backend non géré : ' + cfg.type);
}
