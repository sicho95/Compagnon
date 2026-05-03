import { lsGet } from '../storage/agents-db.js';

let activeBackends     = {};
let _groqDynamicModels = null; // Cache mémoire des modèles Groq (évite les appels répétés)

// ─── Backends statiques de base ──────────────────────────────────────────────
//
// Utilisés si le fetch dynamique Groq échoue ou si aucune clé n'est renseignée.
// La liste Groq est enrichie automatiquement par loadGroqModels() au démarrage.
//
// Clé unique GROQ_API_KEY pour TOUS les modèles Groq (llama, mixtral,
// gemma, qwen, deepseek, kimi, etc.) — pas de duplication de champ.

const DEFAULT_BACKENDS = {
  'groq-llama': {
    label:          'Groq — llama-3.3-70b-versatile',
    type:           'openai-compatible',
    baseUrl:        'https://api.groq.com/openai/v1',
    chatPath:       '/chat/completions',
    model:          'llama-3.3-70b-versatile',
    requiresApiKey: true,
    envKey:         'GROQ_API_KEY',  // Clé unique pour tous les backends Groq
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
//
// Récupère la liste complète des LLMs actifs via GET /openai/v1/models.
// Une seule clé GROQ_API_KEY suffit pour tous les modèles disponibles.
// Cache en mémoire — appel unique par session.
//
// Exclusions :
//   - Whisper / distil-whisper (STT, pas LLM de chat)
//   - Llama Guard (classification, pas LLM de chat)

export async function loadGroqModels() {
  const apiKey = (lsGet('GROQ_API_KEY') || '').trim();
  if (!apiKey)            return; // Pas de clé → backends statiques uniquement
  if (_groqDynamicModels) return; // Déjà chargé cette session

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

    // Supprimer les anciens backends groq-dynamic-* (re-chargement propre)
    Object.keys(activeBackends)
      .filter(k => k.startsWith('groq-dynamic-'))
      .forEach(k => delete activeBackends[k]);

    // Créer un backend par modèle Groq — clé unique GROQ_API_KEY pour tous
    for (const m of models) {
      const id = 'groq-dynamic-' + m.id.replace(/[^a-z0-9-]/gi, '-');
      activeBackends[id] = {
        label:          'Groq — ' + m.id,
        type:           'openai-compatible',
        baseUrl:        'https://api.groq.com/openai/v1',
        chatPath:       '/chat/completions',
        model:          m.id,
        requiresApiKey: true,
        envKey:         'GROQ_API_KEY',  // Clé unique
        groqDynamic:    true,
      };
    }

    // Garder groq-llama comme alias pointant sur le modèle par défaut
    if (activeBackends['groq-llama']) {
      activeBackends['groq-llama'].model = 'llama-3.3-70b-versatile';
      activeBackends['groq-llama'].label = 'Groq — llama-3.3-70b-versatile (défaut)';
    }

    console.info('[Nestor/backends] Groq : ' + models.length + ' modèles chargés dynamiquement.');
  } catch (e) {
    console.warn('[Nestor/backends] Chargement modèles Groq échoué :', e.message);
    // Non bloquant — on reste sur les backends statiques
  }
}

/**
 * Réinitialise le cache Groq.
 * À appeler depuis l'UI Réglages quand l'utilisateur change sa GROQ_API_KEY.
 */
export async function resetGroqModelsCache() {
  _groqDynamicModels = null;
  Object.keys(activeBackends)
    .filter(k => k.startsWith('groq-dynamic-'))
    .forEach(k => delete activeBackends[k]);
  await loadGroqModels(); // Rechargement immédiat avec la nouvelle clé
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
  await loadGroqModels(); // Enrichissement dynamique au démarrage
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

// ─── callLLM ──────────────────────────────────────────────────────────────────

export async function callLLM(backendId, { messages, agentConfig }) {
  const cfg = activeBackends[backendId] || activeBackends['groq-llama'];
  if (!cfg) throw new Error('Aucun backend LLM disponible.');

  // ── Puter (sans clé, secours) ────────────────────────────────────────────
  if (cfg.type === 'puter-qwen' || cfg.type === 'puter-gpt4o') {
    const puter = await waitForPuter();
    const model = cfg.model || (cfg.type === 'puter-gpt4o' ? 'gpt-4o' : 'qwen/qwen-plus');
    const res   = await puter.ai.chat(messages, { model });
    const content = typeof res === 'string' ? res
      : res?.message?.content || res?.content || res?.toString() || '';
    return { message: { role: 'assistant', content } };
  }

  // ── OpenAI-compatible (Groq, Perplexity…) ────────────────────────────────
  if (cfg.type === 'openai-compatible') {
    // Clé unique GROQ_API_KEY pour tous les backends Groq (dynamiques ou statiques)
    const apiKey = cfg.envKey ? (lsGet(cfg.envKey) || '').trim() : '';
    if (cfg.requiresApiKey && !apiKey)
      throw new Error('Clé API manquante pour "' + cfg.label + '". Configure-la dans Réglages.');

    const res = await fetch(cfg.baseUrl + cfg.chatPath, {
      method:  'POST',
      headers: {
        'Content-Type': 'application/json',
        ...(apiKey ? { Authorization: 'Bearer ' + apiKey } : {}),
      },
      body:   JSON.stringify({ model: cfg.model, messages }),
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
