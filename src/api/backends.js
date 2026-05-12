import { lsGet } from '../storage/agents-db.js';
import { getNestorSettings, getBourseSettings, getMeteoSettings } from '../core/settings-store.js';

let activeBackends     = {};
let _groqDynamicModels = null;

// ─── Lecture centralisée des clés API ────────────────────────────────────────
// Toujours passer par settings-store pour éviter le mismatch lsGet/nsKey

function getApiKey(envKey) {
  switch (envKey) {
    case 'GROQ_API_KEY':       return (getNestorSettings().apiKey          || '').trim();
    case 'PERPLEXITY_API_KEY': return (getNestorSettings().openrouterApiKey || '').trim(); // à adapter si clé dédiée
    case 'GEMINI_API_KEY':     return (getNestorSettings().geminiApiKey     || '').trim();
    case 'SERPER_API_KEY':     return (getNestorSettings().serperApiKey     || '').trim();
    default:                   return (lsGet(envKey)                        || '').trim();
  }
}

// ─── Backends statiques de base ──────────────────────────────────────────────

const DEFAULT_BACKENDS = {
  'groq-llama': {
    label:          'Groq — llama-3.3-70b-versatile',
    type:           'openai-compatible',
    baseUrl:        'https://api.groq.com/openai/v1',
    chatPath:       '/chat/completions',
    model:          'llama-3.3-70b-versatile',
    requiresApiKey: true,
    envKey:         'GROQ_API_KEY',
    groqDynamic:    true,
  },
  'puter-qwen': {
    label:          'Puter Qwen (secours sans clé)',
    type:           'puter-qwen',
    model:          'qwen/qwen-plus',
    requiresApiKey: false,
  },
  'perplexity-sonar': {
    label:          'Perplexity Sonar (web)',
    type:           'openai-compatible',
    baseUrl:        'https://api.perplexity.ai',
    chatPath:       '/chat/completions',
    model:          'sonar',
    requiresApiKey: true,
    envKey:         'PERPLEXITY_API_KEY',
  },
};

// ─── Fetch dynamique des modèles Groq ────────────────────────────────────────

export async function loadGroqModels() {
  const apiKey = getApiKey('GROQ_API_KEY');
  if (!apiKey)            return;
  if (_groqDynamicModels) return;

  try {
    const res = await fetch('https://api.groq.com/openai/v1/models', {
      headers: { Authorization: 'Bearer ' + apiKey },
      signal:  AbortSignal.timeout(8000),
    });
    if (!res.ok) throw new Error('HTTP ' + res.status);

    const data   = await res.json();
    const models = (data.data || [])
      .filter(m => m.id
        && !m.id.includes('whisper')
        && !m.id.includes('distil')
        && !m.id.includes('guard')
      )
      .sort((a, b) => a.id.localeCompare(b.id));

    if (models.length === 0) return;
    _groqDynamicModels = models;

    Object.keys(activeBackends)
      .filter(k => k.startsWith('groq-dynamic-'))
      .forEach(k => delete activeBackends[k]);

    for (const m of models) {
      const id = 'groq-dynamic-' + m.id.replace(/[^a-z0-9-]/gi, '-');
      activeBackends[id] = {
        label:          'Groq — ' + m.id,
        type:           'openai-compatible',
        baseUrl:        'https://api.groq.com/openai/v1',
        chatPath:       '/chat/completions',
        model:          m.id,
        requiresApiKey: true,
        envKey:         'GROQ_API_KEY',
        groqDynamic:    true,
      };
    }

    if (activeBackends['groq-llama']) {
      activeBackends['groq-llama'].model = 'llama-3.3-70b-versatile';
      activeBackends['groq-llama'].label = 'Groq — llama-3.3-70b-versatile (défaut)';
    }

    console.info('[Nestor/backends] Groq : ' + models.length + ' modèles chargés dynamiquement.');
  } catch (e) {
    console.warn('[Nestor/backends] Chargement modèles Groq échoué :', e.message);
  }
}

export async function resetGroqModelsCache() {
  _groqDynamicModels = null;
  Object.keys(activeBackends)
    .filter(k => k.startsWith('groq-dynamic-'))
    .forEach(k => delete activeBackends[k]);
  await loadGroqModels();
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
  await loadGroqModels();
}

export function listBackends() {
  return Object.entries(activeBackends).map(([id, cfg]) => ({ id, ...cfg }));
}

// ─── Puter loader ─────────────────────────────────────────────────────────────

function waitForPuter(timeout = 8000) {
  return new Promise((resolve, reject) => {
    if (window.puter?.ai) return resolve(window.puter);
    const start = Date.now();
    const check = setInterval(() => {
      if (window.puter?.ai) { clearInterval(check); resolve(window.puter); }
      else if (Date.now() - start > timeout) { clearInterval(check); reject(new Error('Puter.js non chargé.')); }
    }, 100);
  });
}

// ─── Nettoyage des messages avant envoi ──────────────────────────────────────
// Groq (et d'autres API OpenAI-compatibles) rejettent les messages system
// avec un content vide ou null → 400 Bad Request.

function sanitizeMessages(messages) {
  return messages.filter(m => {
    if (!m || typeof m !== 'object') return false;
    const content = (m.content || '').trim();
    // Supprimer les messages system vides — les autres rôles gardent leur place
    if (m.role === 'system' && !content) return false;
    return true;
  });
}

// ─── callLLM ──────────────────────────────────────────────────────────────────

export async function callLLM(backendId, { messages, agentConfig, tools }) {
  const cfg = activeBackends[backendId] || activeBackends['groq-llama'];
  if (!cfg) throw new Error('Aucun backend LLM disponible.');

  if (cfg.type === 'puter-qwen' || cfg.type === 'puter-gpt4o') {
    const puter = await waitForPuter();
    const model = cfg.model || (cfg.type === 'puter-gpt4o' ? 'gpt-4o' : 'qwen/qwen-plus');
    const res   = await puter.ai.chat(sanitizeMessages(messages), { model });
    const content = typeof res === 'string' ? res
      : res?.message?.content || res?.content || res?.toString() || '';
    return { message: { role: 'assistant', content } };
  }

  if (cfg.type === 'openai-compatible') {
    // Lecture via settings-store (cohérent avec la sauvegarde UI)
    const apiKey = cfg.envKey ? getApiKey(cfg.envKey) : '';
    if (cfg.requiresApiKey && !apiKey)
      throw new Error('Clé API manquante pour "' + cfg.label + '". Configure-la dans Réglages.');

    const cleanMessages = sanitizeMessages(messages);

    const res = await fetch(cfg.baseUrl + cfg.chatPath, {
      method:  'POST',
      headers: {
        'Content-Type': 'application/json',
        ...(apiKey ? { Authorization: 'Bearer ' + apiKey } : {}),
      },
      body:   JSON.stringify({ model: cfg.model, messages: cleanMessages, ...(tools?.length ? { tools } : {}) }),
      signal: AbortSignal.timeout(30000),
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
