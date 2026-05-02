/**
 * orchestrator-engine.js — Nestor PWA
 * Méta-orchestrateur adaptatif v2
 *
 * Pipeline de décision (priorité résultat > coût) :
 *  0. Pré-analyse rapide sans LLM (règles heuristiques)
 *  1. Lookup registre → agent mixte couvrant 80%+ du besoin   → DIRECT
 *  2. Lookup registre → chaîne d'agents existants             → CHAIN
 *  3. Coût chaîne > coût agent mixte dédié ?                  → CREATE_MIXED
 *  4. Sinon                                                    → CREATE_AGENT + CHAIN
 *  5. Fallback                                                 → DIRECT orchestrateur
 *
 * Chaque stratégie mesure sa confiance post-exécution.
 * Si confiance < seuil → escalade automatique vers stratégie supérieure.
 */

import { callLLM }    from '../api/backends.js';
import { searchWeb }  from '../api/search.js';
import { saveAgent, loadAgents } from '../storage/agents-db.js';

// ─── Types de stratégie ───────────────────────────────────────────────────────
export const STRATEGY = {
  DIRECT_MIXED:   'direct_mixed',    // agent mixte existant couvre tout
  DIRECT_LLM:     'direct_llm',      // agent LLM pur existant couvre tout
  CHAIN:          'chain',           // chaîne d'agents existants
  CREATE_MIXED:   'create_mixed',    // créer un agent mixte dédié (plus économique que chain)
  CREATE_AGENT:   'create_agent',    // créer un agent manquant puis exécuter
  DIRECT_ORCH:    'direct_orch',     // fallback orchestrateur direct
};

export const ROLES = {
  WEB_ANALYST:  'web-analyst',
  WEB_SEARCH:   'web-search',
  ORCHESTRATOR: 'orchestrator',
  FACTORY:      'factory',
  GARDENER:     'gardener',
};

// ─── Registre d'agents enrichi ───────────────────────────────────────────────
// Calcule un score de couverture 0-100 pour un agent face à une demande
function coverageScore(agent, userMsg, needsWeb) {
  if (!agent || agent.role === ROLES.ORCHESTRATOR) return 0;
  const msg = userMsg.toLowerCase();
  const tags = (agent.tags || []).map(t => t.toLowerCase());
  const desc = (agent.description || '').toLowerCase();

  // Bonus web
  const isWebAgent = agent.role === ROLES.WEB_ANALYST || agent.role === ROLES.WEB_SEARCH;
  if (needsWeb && !isWebAgent) return 0; // exclure agents LLM si web requis

  // Score par correspondance tags/desc
  const tokens = msg.split(/\s+/).filter(t => t.length > 3);
  let score = 0;
  for (const token of tokens) {
    if (tags.some(t => t.includes(token) || token.includes(t))) score += 15;
    if (desc.includes(token)) score += 8;
  }
  if (isWebAgent && needsWeb) score += 20;
  return Math.min(score, 100);
}

// ─── Heuristique rapide (sans LLM) ──────────────────────────────────────────
const WEB_KEYWORDS = ['actualit', 'météo', 'cours ', 'prix ', 'aujourd', 'dernier', 'récent', 'cote ', 'bourse', 'live', 'direct', 'tv ', 'programme', 'horaire', 'news'];
const MULTI_KEYWORDS = ['puis', 'ensuite', 'après', 'et aussi', 'en plus', 'compare', 'synthèse', 'rapport', 'd\'abord'];

function quickAnalyze(userMsg) {
  const m = userMsg.toLowerCase();
  return {
    needsWeb:    WEB_KEYWORDS.some(k => m.includes(k)),
    isMultiStep: MULTI_KEYWORDS.some(k => m.includes(k)) || userMsg.length > 200,
    wordCount:   userMsg.split(/\s+/).length,
  };
}

// ─── Coût estimé (tokens approximatifs) ─────────────────────────────────────
function estimateCost(strategy, agentCount) {
  const BASE = { direct_mixed: 1200, direct_llm: 800, chain: 0, create_mixed: 2000, create_agent: 1800, direct_orch: 900 };
  if (strategy === STRATEGY.CHAIN) return 900 * agentCount;
  return BASE[strategy] || 1000;
}

// ─── Prompt méta-planification ───────────────────────────────────────────────
function buildMetaPlanPrompt(userMsg, agents, quickAnalysis) {
  const agentList = agents
    .filter(a => a.role !== ROLES.ORCHESTRATOR && a.role !== ROLES.GARDENER)
    .map(a => `{"id":"${a.id}","name":"${a.name}","role":"${a.role}","tags":[${(a.tags||[]).map(t=>`"${t}"`).join(',')}],"perf":${(a.metrics?.confidence||1).toFixed(2)}}`);

  return `Tu es le méta-planificateur de Nestor. Analyse la demande et renvoie UN JSON de plan.

AGENTS DISPONIBLES :
${agentList.join('\n')}

DEMANDE : "${userMsg}"
ANALYSE RAPIDE : ${JSON.stringify(quickAnalysis)}

STRATÉGIES DISPONIBLES (par priorité coût croissant) :
1. "direct_mixed"  — agent mixte existant couvre tout (1 appel)
2. "direct_llm"    — agent LLM pur existant (1 appel, pas de web)
3. "chain"         — chaîne d'agents existants (N appels séquentiels, max 3)
4. "create_mixed"  — créer agent mixte dédié si chain coûterait plus (1 création + 1 appel)
5. "create_agent"  — créer agent manquant + chaîner (si aucun agent ne couvre)
6. "direct_orch"   — fallback orchestrateur direct

RÈGLE ABSOLUE : choisir la stratégie la moins coûteuse qui GARANTIT le résultat.
Préférer "direct_mixed" quand un agent web-analyst existe et couvre le besoin.
"create_mixed" uniquement si chain > 2 agents ET le besoin est récurrent/générique.
"chain" max 3 agents.

RÉPONSE JSON STRICT :
{
  "strategy": "...",
  "reasoning": "courte explication",
  "agents": ["id1","id2"],
  "search_query": "requête web optimisée si web impliqué",
  "confidence_threshold": 0.7,
  "new_agent": null | {"name":"...","role":"web-analyst"|"generic"|"slug","description":"...","system_prompt":"...","tags":[],"backendId":"groq-llama"}
}`;
}

// ─── Runners ─────────────────────────────────────────────────────────────────
async function runWebAnalyst(agent, userMsg, searchQuery, context = '') {
  const results = await searchWeb(searchQuery || userMsg, { maxResults: 6 });
  const webCtx = results.length
    ? results.map((r, i) => `[${i+1}] ${r.title}\n${r.link}\n${r.snippet}`).join('\n\n')
    : 'Aucun résultat web.';
  const prefs = (agent.preferences || []).join('\n');
  const sys = agent.system_prompt
    + (prefs  ? `\n\nPréférences :\n${prefs}` : '')
    + `\n\nRÉSULTATS WEB :\n${webCtx}`
    + (context ? `\n\nCONTEXTE :\n${context}` : '');
  const r = await callLLM(agent.backendId || 'groq-llama', {
    messages: [{ role: 'system', content: sys }, { role: 'user', content: userMsg }],
    agentConfig: agent,
  });
  return r?.message?.content || '(pas de réponse)';
}

async function runLlmAgent(agent, userMsg, context = '') {
  const prefs = (agent.preferences || []).join('\n');
  const sys = agent.system_prompt
    + (prefs  ? `\n\nPréférences :\n${prefs}` : '')
    + (context ? `\n\nCONTEXTE :\n${context}` : '');
  const r = await callLLM(agent.backendId || 'groq-llama', {
    messages: [{ role: 'system', content: sys }, { role: 'user', content: userMsg }],
    agentConfig: agent,
  });
  return r?.message?.content || '(pas de réponse)';
}

// Évalue la confiance d'une réponse (heuristique légère sans LLM)
function assessConfidence(reply, userMsg) {
  if (!reply || reply.length < 30) return 0.2;
  if (reply.includes('(pas de réponse)') || reply.includes('je ne sais pas')) return 0.3;
  const hasStructure = reply.includes('\n') || reply.includes('-') || reply.includes('•');
  const lengthScore = Math.min(reply.length / 500, 1);
  return Math.min(0.5 + (hasStructure ? 0.3 : 0) + lengthScore * 0.2, 1.0);
}

async function createAgentSpec(factoryAgent, newAgentSpec, backendId) {
  if (newAgentSpec?.system_prompt) {
    return {
      id: 'agent-' + Date.now(),
      name: newAgentSpec.name, role: newAgentSpec.role || 'generic',
      description: newAgentSpec.description || '',
      tags: newAgentSpec.tags || [],
      backendId: newAgentSpec.backendId || backendId || 'groq-llama',
      system_prompt: newAgentSpec.system_prompt,
      memory_profile: { level: 'normal' },
      preferences: [], examples: [],
      metrics: { corrections: 0, confidence: 1, lastUsed: null },
      version: 1, createdAt: new Date().toISOString(), updatedAt: new Date().toISOString(),
    };
  }
  if (!factoryAgent) throw new Error('Agent Fabrique introuvable.');
  const r = await callLLM(factoryAgent.backendId || 'groq-llama', {
    messages: [{ role: 'system', content: factoryAgent.system_prompt },
               { role: 'user', content: 'Brief : ' + (newAgentSpec?.description || 'agent spécialisé') }],
  });
  const raw = r?.message?.content || '';
  const match = raw.match(/\{[\s\S]*\}/);
  if (!match) throw new Error('Fabrique JSON invalide');
  const p = JSON.parse(match[0]);
  return {
    id: 'agent-' + Date.now(), name: p.name, role: p.role || 'generic',
    description: p.description || '', tags: p.tags || [],
    backendId: p.backendId || 'groq-llama', system_prompt: p.system_prompt,
    memory_profile: p.memory_profile || { level: 'normal' },
    preferences: [], examples: [],
    metrics: { corrections: 0, confidence: 1, lastUsed: null },
    version: 1, createdAt: new Date().toISOString(), updatedAt: new Date().toISOString(),
  };
}

// ─── POINT D'ENTRÉE ───────────────────────────────────────────────────────────
/**
 * resolve(userMsg, agents, orchestratorAgent, options?)
 * @returns {{ reply, trace, newAgent?, strategy, confidence }}
 */
export async function resolve(userMsg, agents, orchestratorAgent, options = {}) {
  const trace = [];
  const findAgent = id  => agents.find(a => a.id === id);
  const findByRole = r  => agents.find(a => a.role === r);
  const { confidenceThreshold = 0.65 } = options;

  // ── 0. Analyse rapide heuristique (sans LLM) ─────────────────────────────
  const qa = quickAnalyze(userMsg);
  trace.push({ step: 'quick-analyze', data: qa });

  // Pré-sélection : calculer les scores de couverture
  const scored = agents
    .filter(a => a.role !== ROLES.ORCHESTRATOR && a.role !== ROLES.GARDENER && a.role !== ROLES.FACTORY)
    .map(a => ({ agent: a, score: coverageScore(a, userMsg, qa.needsWeb) }))
    .sort((a, b) => b.score - a.score);

  const bestAgent = scored[0]?.agent;
  const bestScore = scored[0]?.score || 0;
  trace.push({ step: 'coverage-scores', data: scored.slice(0, 4).map(s => ({ id: s.agent.id, score: s.score })) });

  // ── 1. Décision rapide si score élevé (évite appel LLM planificateur) ────
  let plan = null;
  if (bestScore >= 65 && !qa.isMultiStep) {
    const isWebAgent = bestAgent.role === ROLES.WEB_ANALYST || bestAgent.role === ROLES.WEB_SEARCH;
    plan = {
      strategy: isWebAgent ? STRATEGY.DIRECT_MIXED : STRATEGY.DIRECT_LLM,
      reasoning: `Couverture heuristique ${bestScore}% — pas besoin de planification LLM`,
      agents: [bestAgent.id],
      search_query: userMsg,
      confidence_threshold: confidenceThreshold,
      new_agent: null,
    };
    trace.push({ step: 'fast-route', msg: `Score ${bestScore}% → ${plan.strategy} sans LLM planificateur` });
  } else {
    // ── 2. Planification LLM (demande complexe ou score faible) ─────────────
    trace.push({ step: 'planning', msg: 'Planification LLM (demande complexe)…' });
    const planPrompt = buildMetaPlanPrompt(userMsg, agents, qa);
    const planR = await callLLM(
      orchestratorAgent?.backendId || 'groq-llama',
      { messages: [{ role: 'user', content: planPrompt }] }
    );
    const planRaw = planR?.message?.content || '';
    try {
      const m = planRaw.match(/\{[\s\S]*\}/);
      plan = JSON.parse(m?.[0] || '{}');
    } catch {
      plan = { strategy: STRATEGY.DIRECT_ORCH, reasoning: 'Parsing échoué', agents: [], search_query: '', new_agent: null };
    }
    // Validation coût : vérifier si CREATE_MIXED est justifié
    if (plan.strategy === STRATEGY.CREATE_MIXED) {
      const chainCost = estimateCost(STRATEGY.CHAIN, (plan.agents || []).length);
      const mixedCost = estimateCost(STRATEGY.CREATE_MIXED, 1);
      if (chainCost < mixedCost && (plan.agents || []).length <= 2) {
        plan.strategy = STRATEGY.CHAIN;
        trace.push({ step: 'cost-override', msg: `Chain (${chainCost}t) moins cher que CREATE_MIXED (${mixedCost}t)` });
      }
    }
    trace.push({ step: 'plan', data: plan });
  }

  // ── 3. Exécution ──────────────────────────────────────────────────────────
  let reply = '', confidence = 0, newAgentCreated = null;

  try {
    if (plan.strategy === STRATEGY.DIRECT_MIXED) {
      const agent = (plan.agents?.[0] && findAgent(plan.agents[0]))
        || findByRole(ROLES.WEB_ANALYST) || findByRole(ROLES.WEB_SEARCH);
      if (!agent) throw new Error('Aucun agent web disponible');
      trace.push({ step: 'exec', msg: `→ ${agent.name} (web-analyst)` });
      reply = await runWebAnalyst(agent, userMsg, plan.search_query);

    } else if (plan.strategy === STRATEGY.DIRECT_LLM) {
      const agent = plan.agents?.[0] && findAgent(plan.agents[0]);
      if (!agent) throw new Error('Agent LLM introuvable : ' + plan.agents?.[0]);
      trace.push({ step: 'exec', msg: `→ ${agent.name} (llm)` });
      reply = await runLlmAgent(agent, userMsg);

    } else if (plan.strategy === STRATEGY.CHAIN) {
      const ids = plan.agents || [];
      let context = '', lastReply = '';
      for (let i = 0; i < ids.length; i++) {
        const agent = findAgent(ids[i]);
        if (!agent) { trace.push({ step: `chain-skip`, msg: `${ids[i]} introuvable` }); continue; }
        trace.push({ step: `chain-${i+1}`, msg: `→ ${agent.name} (${agent.role})` });
        const stepMsg = i === 0 ? userMsg
          : `Demande : "${userMsg}"\n\nRésultat étape précédente :\n${lastReply}`;
        const isWeb = agent.role === ROLES.WEB_ANALYST || agent.role === ROLES.WEB_SEARCH;
        lastReply = isWeb
          ? await runWebAnalyst(agent, stepMsg, i === 0 ? plan.search_query : lastReply.slice(0, 200), context)
          : await runLlmAgent(agent, stepMsg, context);
        context = `Étape ${i+1} (${agent.name}) :\n${lastReply.slice(0, 600)}`;
      }
      reply = lastReply;

    } else if (plan.strategy === STRATEGY.CREATE_MIXED || plan.strategy === STRATEGY.CREATE_AGENT) {
      const factoryAgent = findByRole(ROLES.FACTORY);
      trace.push({ step: 'create', msg: `Création : ${plan.new_agent?.name || 'nouveau'}` });
      const newAgent = await createAgentSpec(factoryAgent, plan.new_agent, orchestratorAgent?.backendId);
      newAgentCreated = newAgent;
      trace.push({ step: 'created', msg: `"${newAgent.name}" (${newAgent.role}) créé` });
      const isWeb = newAgent.role === ROLES.WEB_ANALYST || newAgent.role === ROLES.WEB_SEARCH;
      reply = isWeb
        ? await runWebAnalyst(newAgent, userMsg, plan.search_query)
        : await runLlmAgent(newAgent, userMsg);
    }

    // ── 4. Évaluation confiance post-exécution ───────────────────────────
    confidence = assessConfidence(reply, userMsg);
    trace.push({ step: 'confidence', value: confidence, threshold: plan.confidence_threshold || confidenceThreshold });

    // ── 5. Escalade si confiance insuffisante ────────────────────────────
    if (confidence < (plan.confidence_threshold || confidenceThreshold) && plan.strategy !== STRATEGY.DIRECT_ORCH) {
      trace.push({ step: 'escalate', msg: `Confiance ${confidence.toFixed(2)} < seuil → escalade vers orchestrateur direct` });
      const prefs = (orchestratorAgent?.preferences || []).join('\n');
      const sys = (orchestratorAgent?.system_prompt || 'Tu es Nestor.')
        + (prefs ? `\n\nPréférences :\n${prefs}` : '')
        + `\n\nTentative précédente (confiance ${confidence.toFixed(2)}) :\n${reply.slice(0, 400)}`;
      const r2 = await callLLM(orchestratorAgent?.backendId || 'groq-llama', {
        messages: [{ role: 'system', content: sys }, { role: 'user', content: userMsg }],
      });
      reply = r2?.message?.content || reply;
      confidence = assessConfidence(reply, userMsg);
      trace.push({ step: 'escalated', confidence });
    }

  } catch (e) {
    trace.push({ step: 'error', msg: e.message });
    // Fallback direct orchestrateur
    trace.push({ step: 'fallback', msg: 'Fallback orchestrateur direct' });
    const sys = orchestratorAgent?.system_prompt || 'Tu es Nestor, assistant personnel.';
    const r = await callLLM(orchestratorAgent?.backendId || 'groq-llama', {
      messages: [{ role: 'system', content: sys }, { role: 'user', content: userMsg }],
    });
    reply = r?.message?.content || '(erreur interne)';
    confidence = assessConfidence(reply, userMsg);
  }

  return {
    reply,
    trace,
    newAgent:  newAgentCreated,
    strategy:  plan.strategy,
    confidence,
  };
}
