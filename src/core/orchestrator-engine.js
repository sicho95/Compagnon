/**
 * orchestrator-engine.js
 * Moteur d'orchestration intelligent de Nestor
 *
 * Stratégie de résolution (par ordre de priorité coût/perf) :
 *  1. Agent mixte web-analyst existant → 1 recherche + 1 LLM  ✅ le moins cher
 *  2. Agent web-search existant        → 1 recherche + 1 LLM
 *  3. Agent LLM spécialisé existant    → 1 LLM seul
 *  4. Chaîne d'agents existants        → N appels LLM séquentiels
 *  5. Création d'un agent manquant     → Fabrique → puis exécution
 *  6. Réponse directe orchestrateur    → fallback
 */

import { callLLM } from '../api/backends.js';
import { searchWeb } from '../api/search.js';
import { saveAgent, loadAgents } from '../storage/agents-db.js';

// ─── Constantes rôles ────────────────────────────────────────────────────────
export const ROLES = {
  WEB_ANALYST:  'web-analyst',   // mixte : recherche + analyse LLM
  WEB_SEARCH:   'web-search',    // recherche brute + synthèse LLM
  ORCHESTRATOR: 'orchestrator',
  FACTORY:      'factory',
};

// ─── Prompt d'analyse de l'orchestrateur (meta-prompt) ───────────────────────
function buildPlanningPrompt(userMsg, agents) {
  const agentList = agents
    .filter(a => a.role !== ROLES.ORCHESTRATOR)
    .map(a => `- id:"${a.id}" nom:"${a.name}" role:"${a.role}" tags:[${(a.tags||[]).join(',')}] desc:"${a.description||''}"`);

  return `Tu es le cerveau de planification de Nestor. Analyse la demande et choisis la meilleure stratégie.

AGENTS DISPONIBLES :
${agentList.join('\n')}

DEMANDE UTILISATEUR : "${userMsg}"

RÈGLES DE DÉCISION (par priorité coût/perf) :
1. Si un agent role="web-analyst" couvre exactement le besoin → utilise-le seul (STRATÉGIE: single_web_analyst)
2. Si la demande nécessite info temps réel + analyse → stratégie single_web_analyst, sinon single_agent
3. Si besoin de plusieurs agents en séquence → chain (max 3 agents, sinon trop cher)
4. Si aucun agent ne couvre le besoin → create_agent (crée l'agent manquant le plus économique : web-analyst si besoin web, sinon llm)
5. Fallback : direct (répondre directement)

FORMAT DE RÉPONSE (JSON strict, rien d'autre) :
{
  "strategy": "single_agent" | "single_web_analyst" | "chain" | "create_agent" | "direct",
  "reasoning": "courte explication",
  "agents": ["agent-id-1", "agent-id-2"],  // liste ordonnée pour chain, ou ["agent-id"] pour single
  "search_query": "requête optimisée pour Serper si web impliqué",
  "new_agent": null | { "name": "...", "role": "web-analyst"|"generic", "description": "...", "system_prompt": "...", "tags": [], "backendId": "groq-llama" }
}`;
}

// ─── Exécuter un agent web-analyst (RAG pattern) ─────────────────────────────
async function runWebAnalyst(agent, userMsg, searchQuery, context = '') {
  const query = searchQuery || userMsg;
  const results = await searchWeb(query, { maxResults: 6 });
  const webCtx = results.length
    ? results.map((r, i) => `[${i+1}] ${r.title}\n${r.link}\n${r.snippet}`).join('\n\n')
    : 'Aucun résultat web disponible.';

  const prefs = (agent.preferences || []).join('\n');
  const sysContent = agent.system_prompt
    + (prefs ? `\n\nPréférences utilisateur :\n${prefs}` : '')
    + `\n\nRÉSULTATS WEB (${results.length} sources) :\n${webCtx}`
    + (context ? `\n\nCONTEXTE PRÉCÉDENT :\n${context}` : '');

  const messages = [
    { role: 'system', content: sysContent },
    { role: 'user',   content: userMsg },
  ];
  const choice = await callLLM(agent.backendId || 'groq-llama', { messages, agentConfig: agent });
  return choice?.message?.content || '(pas de réponse)';
}

// ─── Exécuter un agent LLM pur ────────────────────────────────────────────────
async function runLlmAgent(agent, userMsg, context = '') {
  const prefs = (agent.preferences || []).join('\n');
  const sysContent = agent.system_prompt
    + (prefs ? `\n\nPréférences utilisateur :\n${prefs}` : '')
    + (context ? `\n\nCONTEXTE :\n${context}` : '');

  const messages = [
    { role: 'system', content: sysContent },
    { role: 'user',   content: userMsg },
  ];
  const choice = await callLLM(agent.backendId || 'groq-llama', { messages, agentConfig: agent });
  return choice?.message?.content || '(pas de réponse)';
}

// ─── Créer un agent via la Fabrique ──────────────────────────────────────────
async function createAgentViaFactory(factoryAgent, newAgentSpec, backendId) {
  // Si on a déjà le spec depuis le planner, on l'utilise directement
  if (newAgentSpec && newAgentSpec.system_prompt) {
    return {
      id: 'agent-' + Date.now(),
      name: newAgentSpec.name,
      role: newAgentSpec.role || 'generic',
      description: newAgentSpec.description || '',
      tags: newAgentSpec.tags || [],
      backendId: newAgentSpec.backendId || backendId || 'groq-llama',
      system_prompt: newAgentSpec.system_prompt,
      memory_profile: { level: 'normal' },
      preferences: [], examples: [],
      metrics: { corrections: 0, confidence: 1, lastUsed: null },
      version: 1,
      createdAt: new Date().toISOString(),
      updatedAt: new Date().toISOString(),
    };
  }
  // Sinon demander à la fabrique
  if (!factoryAgent) throw new Error('Agent Fabrique introuvable.');
  const messages = [
    { role: 'system', content: factoryAgent.system_prompt },
    { role: 'user',   content: 'Brief : ' + (newAgentSpec?.description || 'agent spécialisé') },
  ];
  const choice = await callLLM(factoryAgent.backendId || 'groq-llama', { messages });
  const raw = choice?.message?.content || '';
  const match = raw.match(/\{[\s\S]*\}/);
  if (!match) throw new Error('Fabrique : JSON invalide.');
  const parsed = JSON.parse(match[0]);
  return {
    id: 'agent-' + Date.now(),
    name: parsed.name, role: parsed.role || 'generic',
    description: parsed.description || '',
    tags: parsed.tags || [],
    backendId: parsed.backendId || 'groq-llama',
    system_prompt: parsed.system_prompt,
    memory_profile: parsed.memory_profile || { level: 'normal' },
    preferences: [], examples: [],
    metrics: { corrections: 0, confidence: 1, lastUsed: null },
    version: 1,
    createdAt: new Date().toISOString(),
    updatedAt: new Date().toISOString(),
  };
}

// ─── POINT D'ENTRÉE PRINCIPAL ─────────────────────────────────────────────────
/**
 * resolve(userMsg, agents, orchestratorAgent)
 * Retourne { reply, trace, newAgent? }
 * - reply      : réponse finale à afficher
 * - trace      : tableau d'étapes pour debug/affichage
 * - newAgent   : agent créé automatiquement (si applicable, à sauvegarder)
 */
export async function resolve(userMsg, agents, orchestratorAgent) {
  const trace = [];
  let newAgentCreated = null;

  // ── 1. Planification ──────────────────────────────────────────────────────
  trace.push({ step: 'planning', msg: 'Analyse de la demande…' });
  const planPrompt = buildPlanningPrompt(userMsg, agents);
  const planChoice = await callLLM(
    orchestratorAgent?.backendId || 'groq-llama',
    { messages: [{ role: 'user', content: planPrompt }] }
  );
  const planRaw = planChoice?.message?.content || '';

  let plan;
  try {
    const match = planRaw.match(/\{[\s\S]*\}/);
    plan = JSON.parse(match?.[0] || '{}');
  } catch {
    plan = { strategy: 'direct', reasoning: 'Parsing plan échoué', agents: [], search_query: '', new_agent: null };
  }
  trace.push({ step: 'plan', data: plan });

  const findAgent = id => agents.find(a => a.id === id);
  const findByRole = role => agents.find(a => a.role === role);

  // ── 2. Exécution selon stratégie ──────────────────────────────────────────
  try {

    // ── Stratégie : agent mixte web-analyst ─────────────────────────────────
    if (plan.strategy === 'single_web_analyst') {
      const agentId = plan.agents?.[0];
      const agent = (agentId && findAgent(agentId))
        || findByRole(ROLES.WEB_ANALYST)
        || findByRole(ROLES.WEB_SEARCH);
      if (!agent) throw new Error('Aucun agent web disponible.');
      trace.push({ step: 'execute', msg: `Agent web-analyst : ${agent.name}` });
      const reply = await runWebAnalyst(agent, userMsg, plan.search_query);
      return { reply, trace, newAgent: null };
    }

    // ── Stratégie : agent LLM unique ────────────────────────────────────────
    if (plan.strategy === 'single_agent') {
      const agentId = plan.agents?.[0];
      const agent = agentId && findAgent(agentId);
      if (!agent) throw new Error('Agent introuvable : ' + agentId);
      trace.push({ step: 'execute', msg: `Agent LLM : ${agent.name}` });
      const reply = await runLlmAgent(agent, userMsg);
      return { reply, trace, newAgent: null };
    }

    // ── Stratégie : chaîne d'agents ─────────────────────────────────────────
    if (plan.strategy === 'chain') {
      const chainIds = plan.agents || [];
      let context = '';
      let lastReply = '';
      for (let i = 0; i < chainIds.length; i++) {
        const agent = findAgent(chainIds[i]);
        if (!agent) { trace.push({ step: 'chain-skip', msg: `Agent ${chainIds[i]} introuvable` }); continue; }
        trace.push({ step: `chain-${i+1}`, msg: `→ ${agent.name} (${agent.role})` });
        const isLast = i === chainIds.length - 1;
        const stepMsg = i === 0 ? userMsg : `Demande originale : "${userMsg}"\n\nRésultat étape précédente :\n${lastReply}`;

        if (agent.role === ROLES.WEB_ANALYST || agent.role === ROLES.WEB_SEARCH) {
          lastReply = await runWebAnalyst(agent, stepMsg, i === 0 ? plan.search_query : lastReply.slice(0, 200), context);
        } else {
          lastReply = await runLlmAgent(agent, stepMsg, context);
        }
        // Contexte compact pour la prochaine étape
        context = isLast ? '' : `Étape ${i+1} (${agent.name}) :\n${lastReply.slice(0, 600)}`;
      }
      return { reply: lastReply, trace, newAgent: null };
    }

    // ── Stratégie : créer un agent manquant puis exécuter ───────────────────
    if (plan.strategy === 'create_agent') {
      const factoryAgent = findByRole(ROLES.FACTORY);
      trace.push({ step: 'create', msg: `Création agent : ${plan.new_agent?.name || 'nouveau'}` });
      const newAgent = await createAgentViaFactory(
        factoryAgent, plan.new_agent, orchestratorAgent?.backendId
      );
      newAgentCreated = newAgent;
      trace.push({ step: 'created', msg: `Agent "${newAgent.name}" créé (rôle: ${newAgent.role})` });

      // Exécuter immédiatement le nouvel agent
      let reply;
      if (newAgent.role === ROLES.WEB_ANALYST || newAgent.role === ROLES.WEB_SEARCH) {
        reply = await runWebAnalyst(newAgent, userMsg, plan.search_query);
      } else {
        reply = await runLlmAgent(newAgent, userMsg);
      }
      return { reply, trace, newAgent: newAgentCreated };
    }

  } catch (e) {
    trace.push({ step: 'error', msg: e.message });
    // Fallback gracieux
  }

  // ── Stratégie : réponse directe (fallback) ──────────────────────────────
  trace.push({ step: 'direct', msg: 'Réponse directe orchestrateur' });
  const prefs = (orchestratorAgent?.preferences || []).join('\n');
  const sysDirect = (orchestratorAgent?.system_prompt || 'Tu es Nestor, assistant personnel.')
    + (prefs ? `\n\nPréférences : ${prefs}` : '');
  const directChoice = await callLLM(
    orchestratorAgent?.backendId || 'groq-llama',
    { messages: [{ role: 'system', content: sysDirect }, { role: 'user', content: userMsg }] }
  );
  return { reply: directChoice?.message?.content || '(sans réponse)', trace, newAgent: null };
}
