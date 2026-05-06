import { listBackends, callLLM, loadGroqModels, resetGroqModelsCache } from '../api/backends.js';
import { saveAgent, deleteAgent, exportAgentsJson, downloadText, importAgentsJson,
         lsGet, lsSet, saveChatHistory, loadChatHistory, clearChatHistory } from '../storage/agents-db.js';
import { gardenerMerge } from '../core/gardener.js';
import { searchWeb, searchWebMulti, getSearchStatus } from '../api/search.js';
import { speak, stopSpeech, isSilentMode, setSilentMode, isSpeechEnabled,
         listBrowserVoices, getTTSStatus } from '../api/tts.js';
import { resolve as orchestratorResolve, ROLES } from '../core/orchestrator-engine.js';
import { renderRadarView, cleanupRadarView } from './radar-view.js';
import { renderBourseView, cleanupBourseView } from './bourse-view.js';

const ROLE_ICONS = {
  orchestrator: '🧠', gardener: '🌿', factory: '🏭',
  'monthly-payments': '📅', 'pea-portfolio': '📈', stories: '📚',
  research: '🔍', 'web-search': '🌐', 'web-analyst': '🔎', generic: '🤖',
};
function roleIcon(role) { return ROLE_ICONS[role] || '🤖'; }
function tag(text, color) {
  const s = document.createElement('span');
  s.textContent = text;
  Object.assign(s.style, { fontSize:'10px', padding:'2px 6px', borderRadius:'10px',
    background: color || '#2a2a2a', color:'#ccc', display:'inline-block', marginRight:'4px' });
  return s;
}

// ─── Sépare la réponse principale des sources ─────────────────────────────────
function splitReplyAndSources(text) {
  const sourcePattern = /\n+(?:Sources?\s*(?:\(s\))?\s*:.*)/si;
  const match = text.match(sourcePattern);
  if (!match) return { main: text, sources: '' };
  const idx = text.indexOf(match[0]);
  return { main: text.slice(0, idx).trimEnd(), sources: match[0].trim() };
}

function textForTTS(text) {
  const { main } = splitReplyAndSources(text);
  return main.replace(/https?:\/\/\S+/g, '').replace(/\s{2,}/g, ' ').trim();
}

export function renderDashboard(container, state, rerender) {
  container.innerHTML = '';

  if (state.view === 'chat' && state.activeAgent) { renderChatView(container, state, rerender); return; }
  if (state.view === 'edit' && state.editingAgent) { renderEditView(container, state, rerender); return; }
  if (state.view === 'fabrique') { renderFabriqueView(container, state, rerender); return; }
  if (state.view === 'radar') { renderRadarSection(container, state, rerender); return; }
  if (state.view === 'bourse') { renderBourseSection(container, state, rerender); return; }

  const header = el('div', { display:'flex', justifyContent:'space-between', alignItems:'center', marginBottom:'10px' });
  const title = el('div', { fontWeight:'600', fontSize:'15px' });
  title.textContent = state.view === 'settings' ? '⚙️ Réglages' : '🤖 Agents';

  const actions = el('div', { display:'flex', gap:'8px', flexWrap:'wrap' });

  if (state.view === 'agents') {
    const btnFabrique = btn('+ Fabrique', 'primary', () => { state.view = 'fabrique'; rerender(); });
    const btnJardinier = btn('🌿 Jardinier', '', async () => {
      btnJardinier.textContent = '⏳ En cours…';
      btnJardinier.disabled = true;
      try {
        const merged = await gardenerMerge(state.agents, []);
        for (const a of merged) await saveAgent(a);
        state.agents = merged;
        showToast('Jardinier : ' + merged.length + ' agent(s) révisé(s).');
      } catch(e) { showToast('Erreur Jardinier : ' + e.message, true); }
      rerender();
    });
    const btnExportAll = btn('⬇ Export', '', () => { downloadText('nestor-agents.json', exportAgentsJson(state.agents)); });
    const btnImport = btn('⬆ Import', '', () => {
      const input = document.createElement('input');
      input.type = 'file'; input.accept = 'application/json';
      input.onchange = async () => {
        try {
          const text = await input.files[0].text();
          state.agents = await importAgentsJson(text, gardenerMerge);
          showToast('Import OK : ' + state.agents.length + ' agents.');
          rerender();
        } catch(e) { showToast('Erreur import : ' + e.message, true); }
      };
      input.click();
    });
    actions.append(btnFabrique, btnJardinier, btnExportAll, btnImport);
  }

  header.append(title, actions);
  container.appendChild(header);

  if (state.view === 'agents') renderAgentsList(container, state, rerender);
  else renderSettings(container, state, rerender);
}

// ─── Radar section ────────────────────────────────────────────────────────────
function renderRadarSection(container, state, rerender) {
  const header = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'12px' });
  const title = el('div', { fontWeight:'600', fontSize:'15px', flex:'1' });
  title.textContent = '🚨 Radars — Surveillance';
  header.appendChild(title);

  const backBtn = btn('← Retour', '', () => {
    cleanupRadarView();
    state.view = state._radarPrevView || 'chat';
    rerender();
  });
  header.appendChild(backBtn);
  container.appendChild(header);

  const body = el('div', {});
  container.appendChild(body);

  renderRadarView(body, state, rerender, () => {
    state.view = state._radarPrevView || 'chat';
    rerender();
  });
}

// ─── Bourse section ───────────────────────────────────────────────────────────
function renderBourseSection(container, state, rerender) {
  const header = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'12px' });
  const title = el('div', { fontWeight:'600', fontSize:'15px', flex:'1' });
  title.textContent = '📈 Bourse & Marchés';
  header.appendChild(title);

  const backBtn = btn('← Retour', '', () => {
    cleanupBourseView(container.querySelector('[data-bourse]'));
    state.view = state._boursePrevView || 'chat';
    rerender();
  });
  header.appendChild(backBtn);
  container.appendChild(header);

  const body = el('div', {});
  body.setAttribute('data-bourse', '1');
  container.appendChild(body);

  renderBourseView(body, state, rerender);
}

// ─── Agents List ──────────────────────────────────────────────────────────────
function renderAgentsList(container, state, rerender) {
  if (!state.agents || state.agents.length === 0) {
    const empty = el('div', { textAlign:'center', padding:'40px 0', color:'#666' });
    empty.textContent = 'Aucun agent. Utilisez la Fabrique pour en créer un.';
    container.appendChild(empty);
    return;
  }

  const list = el('div', { display:'flex', flexDirection:'column', gap:'10px' });

  state.agents.forEach((agent) => {
    const card = el('div', {
      background:'#1a1a1a', border:'1px solid #2a2a2a', borderRadius:'10px',
      padding:'12px', display:'flex', flexDirection:'column', gap:'6px'
    });

    const topRow = el('div', { display:'flex', alignItems:'center', gap:'8px' });
    const icon = el('span', { fontSize:'20px' }); icon.textContent = roleIcon(agent.role);
    const nameEl = el('div', { fontWeight:'600', fontSize:'14px', flex:'1' });
    nameEl.textContent = agent.name;
    const roleBadgeColor = agent.role === ROLES.WEB_ANALYST ? '#1a2a3a' :
                           agent.role === ROLES.WEB_SEARCH  ? '#1a1a3a' : '#1c2a1c';
    const backendBadge = tag(agent.backendId || 'groq-llama', roleBadgeColor);
    if (agent.role === ROLES.WEB_ANALYST) {
      const mixBadge = tag('WEB+LLM', '#1a3a2a');
      topRow.append(icon, nameEl, mixBadge, backendBadge);
    } else {
      topRow.append(icon, nameEl, backendBadge);
    }

    const descEl = el('div', { fontSize:'12px', color:'#888', lineHeight:'1.4' });
    descEl.textContent = agent.description || '';

    const tagsRow = el('div', { display:'flex', flexWrap:'wrap', gap:'4px' });
    (agent.tags || []).forEach(t => tagsRow.appendChild(tag(t)));

    card.append(topRow, descEl, tagsRow);

    if (agent.preferences && agent.preferences.length > 0) {
      const prefEl = el('div', { fontSize:'11px', color:'#5a9', fontStyle:'italic' });
      prefEl.textContent = '📋 ' + agent.preferences.slice(-2).join(' • ');
      card.appendChild(prefEl);
    }

    const btnsRow = el('div', { display:'flex', gap:'6px', flexWrap:'wrap', marginTop:'4px' });

    const btnChat = btn('💬 Parler', 'primary', () => {
      if (!agent.backendId) agent.backendId = 'groq-llama';
      state.activeAgent = agent;
      // Charge l'historique persisté
      const hist = loadChatHistory(agent.id);
      state.chatHistory = [{ role:'system', content: agent.system_prompt || '' }, ...hist];
      state.view = 'chat';
      rerender();
    });

    const btnEdit = btn('✏️ Éditer', '', () => {
      state.editingAgent = JSON.parse(JSON.stringify(agent));
      state.view = 'edit';
      rerender();
    });

    const btnClearHist = btn('🗑 Historique', '', async () => {
      if (!confirm('Effacer l\'historique de ' + agent.name + ' ?')) return;
      clearChatHistory(agent.id);
      if (state.activeAgent?.id === agent.id) {
        state.chatHistory = [{ role:'system', content: agent.system_prompt || '' }];
      }
      showToast('Historique de ' + agent.name + ' effacé.');
    });
    btnClearHist.title = 'Effacer l\'historique de conversation';
    btnClearHist.style.fontSize = '11px';

    const btnDel = btn('🗑', '', async () => {
      if (!confirm('Supprimer ' + agent.name + ' ?')) return;
      await deleteAgent(agent.id);
      clearChatHistory(agent.id);
      state.agents = state.agents.filter(a => a.id !== agent.id);
      rerender();
    });
    btnDel.style.marginLeft = 'auto';

    btnsRow.append(btnChat, btnEdit, btnClearHist, btnDel);
    card.appendChild(btnsRow);
    list.appendChild(card);
  });

  container.appendChild(list);
}

// ─── Chat View ────────────────────────────────────────────────────────────────
function renderChatView(container, state, rerender) {
  const agent = state.activeAgent;
  const isOrchestrator = agent.role === ROLES.ORCHESTRATOR;

  // Initialise l'historique depuis la persistence si seulement le message système est présent
  if (!state.chatHistory || state.chatHistory.filter(m => m.role !== 'system').length === 0) {
    const hist = loadChatHistory(agent.id);
    state.chatHistory = [{ role:'system', content: agent.system_prompt || '' }, ...hist];
  }

  const header = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'10px' });
  const title = el('div', { fontWeight:'600', fontSize:'15px', flex:'1' });
  title.textContent = roleIcon(agent.role) + ' ' + agent.name;
  if (isOrchestrator) {
    const badge = tag('Moteur actif', '#1a3a2a');
    badge.style.color = '#7ef';
    title.appendChild(document.createTextNode(' '));
    title.appendChild(badge);
  }

  // Bouton historique : count des messages non-système
  const histCount = state.chatHistory.filter(m => m.role !== 'system').length;
  if (histCount > 0) {
    const histBadge = el('div', { fontSize:'10px', color:'#555', cursor:'pointer' });
    histBadge.textContent = histCount + ' msg';
    histBadge.title = 'Effacer l\'historique';
    histBadge.onclick = () => {
      if (!confirm('Effacer l\'historique de conversation ?')) return;
      clearChatHistory(agent.id);
      state.chatHistory = [{ role:'system', content: agent.system_prompt || '' }];
      rerender();
    };
    header.appendChild(histBadge);
  }

  const silentBtn = btn(isSilentMode() ? '🔇' : '🔊', '', () => {
    const newMode = !isSilentMode();
    setSilentMode(newMode);
    silentBtn.textContent = newMode ? '🔇' : '🔊';
    silentBtn.title = newMode ? 'Mode silencieux actif' : 'Voix activée';
    if (newMode) stopSpeech();
    showToast(newMode ? 'Mode silencieux activé.' : 'Voix activée.');
  });
  silentBtn.title = isSilentMode() ? 'Mode silencieux actif' : 'Voix activée';
  Object.assign(silentBtn.style, { fontSize:'16px', padding:'4px 8px', background:'#222', border:'1px solid #333' });

  header.append(title, silentBtn);
  container.appendChild(header);

  const msgArea = el('div', {
    flex:'1', overflowY:'auto', display:'flex', flexDirection:'column', gap:'8px',
    padding:'8px 0', maxHeight:'55vh', minHeight:'120px'
  });

  let tracePanel = null;
  if (isOrchestrator) {
    tracePanel = el('div', { display:'none', background:'#0a1a0a', border:'1px solid #1a3a1a',
      borderRadius:'8px', padding:'8px', marginBottom:'6px', fontSize:'11px', color:'#4a8a4a' });
    tracePanel.id = 'orch-trace';
  }

  const renderMessages = () => {
    msgArea.innerHTML = '';
    state.chatHistory.filter(m => m.role !== 'system').forEach(m => {
      const bubble = el('div', {
        maxWidth:'85%', padding:'8px 12px', borderRadius:'12px', fontSize:'13px', lineHeight:'1.5',
        alignSelf: m.role === 'user' ? 'flex-end' : 'flex-start',
        background: m.role === 'user' ? '#1a3a2a' : (m._orchestrated ? '#1a1a3a' : '#1e1e2e'),
        color: '#ddd', whiteSpace:'pre-wrap'
      });

      if (m.role === 'assistant') {
        const { main, sources } = splitReplyAndSources(m.content);
        bubble.textContent = main;
        if (sources) {
          const srcEl = el('div', { marginTop:'10px', paddingTop:'8px', borderTop:'1px solid #2a2a3a',
            fontSize:'10px', color:'#556', lineHeight:'1.5', whiteSpace:'pre-wrap' });
          srcEl.innerHTML = sources.replace(
            /(https?:\/\/[^\s<]+)/g,
            '<a href="$1" target="_blank" rel="noopener noreferrer" style="color:#558;word-break:break-all;">$1</a>'
          );
          bubble.appendChild(srcEl);
        }
      } else {
        bubble.textContent = m.content;
      }

      if (m._strategy) {
        const sb = el('div', { fontSize:'10px', color:'#5a7a9a', marginTop:'4px' });
        sb.textContent = '⚙️ ' + m._strategy;
        bubble.appendChild(sb);
      }
      msgArea.appendChild(bubble);
    });
    msgArea.scrollTop = msgArea.scrollHeight;
  };
  renderMessages();
  container.appendChild(msgArea);
  if (tracePanel) container.appendChild(tracePanel);

  const feedbackZone = el('div', { margin:'4px 0' });
  const feedbackToggle = btn('📝 Corriger / Feedback', '', () => {
    feedbackInput.style.display = feedbackInput.style.display === 'none' ? 'block' : 'none';
    feedbackSend.style.display  = feedbackSend.style.display  === 'none' ? 'inline-block' : 'none';
  });
  feedbackToggle.style.fontSize = '11px';
  const feedbackInput = document.createElement('textarea');
  Object.assign(feedbackInput.style, {
    display:'none', width:'100%', marginTop:'4px', background:'#111', color:'#ccc',
    border:'1px solid #333', borderRadius:'6px', padding:'6px', fontSize:'12px', boxSizing:'border-box'
  });
  feedbackInput.placeholder = 'Correction ou préférence pour cet agent…';
  feedbackInput.rows = 2;
  const feedbackSend = btn('✔ Enregistrer', 'primary', async () => {
    const txt = feedbackInput.value.trim(); if (!txt) return;
    agent.preferences = agent.preferences || [];
    agent.preferences.push(txt);
    agent.metrics = agent.metrics || {};
    agent.metrics.corrections = (agent.metrics.corrections || 0) + 1;
    await saveAgent(agent);
    state.agents = state.agents.map(a => a.id === agent.id ? agent : a);
    feedbackInput.value = '';
    feedbackInput.style.display = 'none';
    feedbackSend.style.display  = 'none';
    showToast('Correction enregistrée pour ' + agent.name);
  });
  feedbackSend.style.display = 'none'; feedbackSend.style.marginTop = '4px';
  feedbackZone.append(feedbackToggle, feedbackInput, feedbackSend);
  container.appendChild(feedbackZone);

  const inputRow = el('div', { display:'flex', gap:'8px', marginTop:'4px' });
  const input = document.createElement('input');
  Object.assign(input.style, {
    flex:'1', background:'#111', color:'#fff', border:'1px solid #333',
    borderRadius:'8px', padding:'8px 10px', fontSize:'14px'
  });
  input.placeholder = 'Ton message…';
  input.setAttribute('autocomplete', 'off');

  const sendBtn = btn('Envoyer', 'primary', send);
  input.addEventListener('keydown', e => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send(); } });

  async function send() {
    const msg = input.value.trim(); if (!msg) return;
    input.value = ''; sendBtn.disabled = true; sendBtn.textContent = '⏳';
    state.chatHistory.push({ role:'user', content: msg });
    renderMessages();

    let reply = '';
    try {
      let strategy = '', traceSteps = [];

      if (isOrchestrator) {
        const { reply: r, trace, newAgent } = await orchestratorResolve(msg, state.agents, agent);
        reply = r;
        traceSteps = trace;
        strategy = trace.find(t => t.step === 'plan')?.data?.strategy || 'direct';

        if (tracePanel) {
          tracePanel.style.display = 'block';
          tracePanel.innerHTML = traceSteps
            .map(t => `<div>▸ [${t.step}] ${t.msg || JSON.stringify(t.data?.strategy || '')}</div>`)
            .join('');
        }

        if (newAgent) {
          const keep = confirm(
            `L'orchestrateur a créé l'agent "${newAgent.name}" (${newAgent.role}).\nL'ajouter définitivement ?`
          );
          if (keep) {
            await saveAgent(newAgent);
            state.agents = state.agents.concat([newAgent]);
            showToast(`Agent "${newAgent.name}" ajouté.`);
            rerender();
          }
        }
        state.chatHistory.push({ role:'assistant', content: reply, _orchestrated: true, _strategy: strategy });

      } else if (agent.role === ROLES.WEB_ANALYST || agent.role === ROLES.WEB_SEARCH) {
        const subQueries = splitIntoSubQueries(msg);
        let context;
        if (subQueries.length > 1) {
          const { allResults } = await searchWebMulti(subQueries, { maxResultsPerQuery: 3 });
          context = allResults.length
            ? allResults.map((r, i) => `[${i+1}] (${r._query})\n${r.title}\n${r.link}\n${r.snippet}`).join('\n\n')
            : 'Aucun résultat web disponible.';
        } else {
          const results = await searchWeb(msg, { maxResults: 6 });
          context = results.length
            ? results.map((r, i) => `[${i+1}] ${r.title}\n${r.link}\n${r.snippet}`).join('\n\n')
            : 'Aucun résultat web disponible.';
        }
        const prefs = (agent.preferences || []).join('\n');
        const sysContent = agent.system_prompt
          + (prefs ? `\n\nPréférences :\n${prefs}` : '')
          + `\n\nRÉSULTATS WEB :\n${context}`;
        const choice = await callLLM(agent.backendId || 'groq-llama', {
          messages: [{ role:'system', content: sysContent }, { role:'user', content: msg }],
          agentConfig: agent,
        });
        reply = choice?.message?.content || '(pas de réponse)';
        state.chatHistory.push({ role:'assistant', content: reply });

      } else {
        const prefs = (agent.preferences || []).join('\n');
        const sysContent = agent.system_prompt + (prefs ? `\n\nPréférences :\n${prefs}` : '');
        const messages = [
          { role:'system', content: sysContent },
          ...state.chatHistory.filter(m => m.role !== 'system'),
        ];
        const choice = await callLLM(agent.backendId || 'groq-llama', { messages, agentConfig: agent });
        reply = choice?.message?.content || '(pas de réponse)';
        state.chatHistory.push({ role:'assistant', content: reply });
      }

    } catch(e) {
      reply = '⚠️ Erreur : ' + e.message;
      state.chatHistory.push({ role:'assistant', content: reply });
    }

    sendBtn.disabled = false; sendBtn.textContent = 'Envoyer';
    renderMessages();
    agent.metrics = agent.metrics || {};
    agent.metrics.lastUsed = new Date().toISOString();
    await saveAgent(agent);

    // Persiste l'historique
    saveChatHistory(agent.id, state.chatHistory);

    if (reply && !isSilentMode()) {
      const ttsText = textForTTS(reply);
      if (ttsText) speak(ttsText).catch(e => console.warn('[Nestor/TTS]', e.message));
    }
  }

  inputRow.append(input, sendBtn);
  container.appendChild(inputRow);
}

// ─── Découpage de requête complexe ───────────────────────────────────────────
const TV_CHAINS = ['TF1','France 2','France2','France 3','France3','Canal+','France 5','France5','M6','Arte','C8','CNews','BFM','TMC'];

function splitIntoSubQueries(query) {
  const foundChains = TV_CHAINS.filter(c => query.toLowerCase().includes(c.toLowerCase()));
  if (foundChains.length >= 3) {
    const base = query.replace(new RegExp(foundChains.join('|'), 'gi'), '').trim()
      .replace(/,|et|\s+/g, ' ').trim();
    return foundChains.map(c => (base ? base + ' ' + c : 'programme ' + c + ' ce soir'));
  }
  if (query.includes('/')) {
    const parts = query.split('/').map(s => s.trim()).filter(Boolean);
    if (parts.length >= 2) return parts;
  }
  return [query];
}

// ─── Fabrique View ────────────────────────────────────────────────────────────
function renderFabriqueView(container, state, rerender) {
  const header = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'12px' });
  const title = el('div', { fontWeight:'600', fontSize:'15px' });
  title.textContent = '🏭 Fabrique d\'agents';
  header.append(title);
  container.appendChild(header);

  const desc = el('div', { fontSize:'12px', color:'#888', marginBottom:'12px', lineHeight:'1.5' });
  desc.textContent = 'Décris l\'agent en quelques mots. La Fabrique choisira automatiquement le bon rôle.';
  container.appendChild(desc);

  const briefLabel = labelEl('Brief');
  const briefInput = document.createElement('textarea');
  Object.assign(briefInput.style, {
    width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
    borderRadius:'8px', padding:'8px', fontSize:'13px', boxSizing:'border-box'
  });
  briefInput.rows = 3;
  briefInput.placeholder = 'Agent pour voir le programme TV ce soir...';
  container.append(briefLabel, briefInput);

  const backendLabel = labelEl('Backend LLM');
  const backendSel = document.createElement('select');
  Object.assign(backendSel.style, {
    width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
    borderRadius:'8px', padding:'8px', fontSize:'13px', marginBottom:'10px'
  });
  listBackends().forEach(b => {
    const opt = document.createElement('option');
    opt.value = b.id; opt.textContent = b.label;
    if (b.id === 'groq-llama') opt.selected = true;
    backendSel.appendChild(opt);
  });
  container.append(backendLabel, backendSel);

  const previewZone = el('div', { display:'none' });
  container.appendChild(previewZone);

  const genBtn = btn('✨ Générer avec la Fabrique', 'primary', async () => {
    const brief = briefInput.value.trim();
    if (!brief) { showToast('Décris l\'agent d\'abord.', true); return; }
    genBtn.textContent = '⏳ Génération…'; genBtn.disabled = true;
    previewZone.innerHTML = ''; previewZone.style.display = 'none';
    try {
      const fabAgent = state.agents.find(a => a.role === 'factory');
      if (!fabAgent) throw new Error('Agent Fabrique introuvable dans le registre.');
      const messages = [
        { role:'system', content: fabAgent.system_prompt },
        { role:'user', content: 'Brief : ' + brief }
      ];
      const choice = await callLLM(fabAgent.backendId || 'groq-llama', { messages });
      const raw = choice?.message?.content || '';
      const match = raw.match(/\{[\s\S]*\}/);
      if (!match) throw new Error('Réponse Fabrique invalide (pas de JSON).');
      const newAgent = JSON.parse(match[0]);
      newAgent.id = 'agent-' + Date.now();
      newAgent.backendId = backendSel.value || 'groq-llama';
      newAgent.preferences = [];
      newAgent.examples = [];
      newAgent.metrics = { corrections: 0, confidence: 1, lastUsed: null };
      newAgent.version = 1;
      newAgent.createdAt = new Date().toISOString();
      newAgent.updatedAt = new Date().toISOString();

      previewZone.style.display = 'block';
      previewZone.innerHTML = '';
      const previewTitle = el('div', { fontWeight:'600', fontSize:'13px', marginBottom:'6px', marginTop:'12px' });
      previewTitle.textContent = 'Prévisualisation :';
      const preCard = el('div', { background:'#1a2a1a', border:'1px solid #2a4a2a', borderRadius:'10px', padding:'12px', fontSize:'12px', lineHeight:'1.6' });
      preCard.innerHTML = '📛 <b>' + esc(newAgent.name) + '</b> <small>(' + esc(newAgent.role || '') + ')</small><br>'
        + '<span style="color:#888">' + esc(newAgent.description || '') + '</span><br><br>'
        + '<details><summary style="cursor:pointer;color:#5a9">Voir le prompt</summary><pre style="white-space:pre-wrap;font-size:11px;color:#aaa;margin-top:6px">'
        + esc(newAgent.system_prompt) + '</pre></details>';
      const confirmBtn = btn('✔ Ajouter cet agent', 'primary', async () => {
        await saveAgent(newAgent);
        state.agents = state.agents.concat([newAgent]);
        showToast('Agent "' + newAgent.name + '" ajouté.');
        state.view = 'agents'; rerender();
      });
      previewZone.append(previewTitle, preCard, confirmBtn);
    } catch(e) { showToast('Erreur Fabrique : ' + e.message, true); }
    genBtn.textContent = '✨ Générer avec la Fabrique'; genBtn.disabled = false;
  });
  container.appendChild(genBtn);
}

// ─── Edit View ────────────────────────────────────────────────────────────────
function renderEditView(container, state, rerender) {
  const agent = state.editingAgent;
  const header = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'12px' });
  const title = el('div', { fontWeight:'600', fontSize:'14px', flex:'1' });
  title.textContent = '✏️ ' + agent.name;
  header.append(title);
  container.appendChild(header);

  const form = el('div', { display:'flex', flexDirection:'column', gap:'10px' });
  const fields = [
    { key:'name', lbl:'Nom', type:'text' },
    { key:'role', lbl:'Rôle (slug)', type:'text' },
    { key:'description', lbl:'Description', type:'textarea' },
    { key:'system_prompt', lbl:'System prompt', type:'textarea', rows:8 },
  ];
  fields.forEach(({ key, lbl, type, rows }) => {
    const lEl = labelEl(lbl);
    let inp;
    if (type === 'textarea') {
      inp = document.createElement('textarea');
      inp.rows = rows || 3;
    } else {
      inp = document.createElement('input');
      inp.type = 'text';
    }
    Object.assign(inp.style, {
      width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
      borderRadius:'8px', padding:'8px', fontSize:'13px', boxSizing:'border-box'
    });
    inp.value = agent[key] || '';
    inp.oninput = () => { agent[key] = inp.value; };
    form.append(lEl, inp);
  });

  const bLabel = labelEl('Backend');
  const bSel = document.createElement('select');
  Object.assign(bSel.style, { width:'100%', background:'#111', color:'#ccc', border:'1px solid #333', borderRadius:'8px', padding:'8px', fontSize:'13px' });
  listBackends().forEach(b => {
    const opt = document.createElement('option');
    opt.value = b.id; opt.textContent = b.label;
    if (b.id === (agent.backendId || 'groq-llama')) opt.selected = true;
    bSel.appendChild(opt);
  });
  bSel.onchange = () => { agent.backendId = bSel.value; };
  form.append(bLabel, bSel);

  if (agent.preferences && agent.preferences.length > 0) {
    const pLabel = labelEl('📋 Préférences apprises (' + agent.preferences.length + ')');
    const pList = el('div', { background:'#111', border:'1px solid #222', borderRadius:'8px', padding:'8px', fontSize:'12px', color:'#5a9' });
    agent.preferences.forEach((p, i) => {
      const row = el('div', { display:'flex', justifyContent:'space-between', alignItems:'flex-start', gap:'6px', marginBottom:'4px' });
      const txt = el('span', {}); txt.textContent = '• ' + p;
      const delBtn = btn('✕', '', () => {
        agent.preferences.splice(i, 1);
        saveAgent(agent);
        state.agents = state.agents.map(a => a.id === agent.id ? agent : a);
        renderEditView(container, state, rerender);
      });
      delBtn.style.cssText = 'background:none;color:#f66;border:none;cursor:pointer;font-size:12px;padding:0;flex-shrink:0';
      row.append(txt, delBtn); pList.appendChild(row);
    });
    form.append(pLabel, pList);
  }

  const saveBtn = btn('💾 Enregistrer', 'primary', async () => {
    await saveAgent(agent);
    state.agents = state.agents.map(a => a.id === agent.id ? agent : a);
    state.view = 'agents'; state.editingAgent = null;
    showToast('Agent "' + agent.name + '" sauvegardé.');
    rerender();
  });
  form.appendChild(saveBtn);
  container.appendChild(form);
}

// ─── Settings View ────────────────────────────────────────────────────────────
function renderSettings(container, state, rerender) {
  const backends = listBackends();
  const list = el('div', { display:'flex', flexDirection:'column', gap:'10px' });

  // ── Note générale ──────────────────────────────────────────────────────────
  const noteEl = el('div', { fontSize:'12px', color:'#888', marginBottom:'8px', lineHeight:'1.5' });
  noteEl.innerHTML = '🔑 Les clés API sont stockées localement sur cet appareil uniquement.<br>Groq est gratuit avec un compte sur <a href="https://console.groq.com" target="_blank" style="color:#5af">console.groq.com</a>.';
  list.appendChild(noteEl);

  // ── Backends LLM ───────────────────────────────────────────────────────────
  backends.forEach((b) => {
    const card = el('div', { background:'#1a1a1a', border:'1px solid #2a2a2a', borderRadius:'10px', padding:'12px' });
    const titleEl = el('div', { fontWeight:'600', fontSize:'13px', marginBottom:'4px' });
    titleEl.textContent = b.label;
    const typeEl = el('div', { fontSize:'11px', color:'#666', marginBottom:'8px' });
    typeEl.textContent = b.type;
    card.append(titleEl, typeEl);

    if (b.requiresApiKey && b.envKey) {
      const lEl = labelEl('Clé API : ' + b.envKey);
      const inp = document.createElement('input');
      inp.type = 'password'; inp.autocomplete = 'off';
      Object.assign(inp.style, {
        width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
        borderRadius:'8px', padding:'8px', fontSize:'13px', boxSizing:'border-box'
      });
      inp.placeholder = b.envKey.includes('GROQ') ? 'gsk_...' : 'sk-...';
      inp.value = lsGet(b.envKey) || '';
      inp.onchange = () => { lsSet(b.envKey, inp.value.trim()); showToast('Clé ' + b.envKey + ' sauvegardée.'); };
      card.append(lEl, inp);
      const statusDot = el('div', { fontSize:'11px', marginTop:'4px' });
      const hasKey = !!(lsGet(b.envKey) || '').trim();
      statusDot.textContent = hasKey ? '✅ Clé présente' : '⚠️ Clé manquante';
      statusDot.style.color = hasKey ? '#5a9' : '#a66';
      card.appendChild(statusDot);
    } else {
      const freeEl = el('div', { fontSize:'12px', color:'#5a9' });
      freeEl.textContent = '✅ Gratuit, aucune clé requise';
      card.appendChild(freeEl);
    }
    list.appendChild(card);
  });

  // ── Section Recherche Web ──────────────────────────────────────────────────
  const searchCard = el('div', { background:'#101018', border:'1px solid #2a2a3a', borderRadius:'10px', padding:'12px', marginTop:'4px' });
  const sTitle = el('div', { fontWeight:'600', fontSize:'13px', marginBottom:'4px' });
  sTitle.textContent = '🌐 Recherche Web';
  const status = getSearchStatus();
  const statusBadge = el('div', { fontSize:'11px', marginBottom:'8px', padding:'4px 8px', borderRadius:'6px',
    background: status.engine === 'serper' ? '#1a3a1a' : '#1a1a3a',
    color: status.engine === 'serper' ? '#7ef' : '#88f', display:'inline-block' });
  statusBadge.textContent = (status.engine === 'serper' ? '🟢 Serper.dev actif' : '🔵 SearXNG fallback') + ' — ' + status.reason;
  searchCard.append(sTitle, statusBadge);

  const sDesc = el('div', { fontSize:'11px', color:'#777', marginBottom:'10px', lineHeight:'1.5' });
  sDesc.innerHTML = 'Stratégie : <b>Serper.dev</b> en 1er (2500 req/mois gratuites) → <b>SearXNG</b> en fallback (illimité, sans clé).<br>Crée un compte sur <a href="https://serper.dev" target="_blank" style="color:#5af">serper.dev</a>.';
  searchCard.appendChild(sDesc);

  const serperLabel = labelEl('Clé Serper.dev (SERPER_KEY) — optionnelle');
  const serperInput = document.createElement('input');
  Object.assign(serperInput.style, {
    width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
    borderRadius:'8px', padding:'8px', fontSize:'13px', boxSizing:'border-box', marginBottom:'6px'
  });
  serperInput.type = 'password';
  serperInput.placeholder = 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx';
  serperInput.value = lsGet('SERPER_KEY') || '';
  serperInput.onchange = () => { lsSet('SERPER_KEY', serperInput.value.trim()); showToast('SERPER_KEY sauvegardée.'); rerender(); };

  const serperStatus = el('div', { fontSize:'11px', marginBottom:'8px' });
  const hasSerper = !!(lsGet('SERPER_KEY') || '').trim();
  serperStatus.textContent = hasSerper ? '✅ Clé Serper présente' : '⚠️ Pas de clé Serper — SearXNG sera utilisé en fallback direct';
  serperStatus.style.color = hasSerper ? '#5a9' : '#a66';

  searchCard.append(serperLabel, serperInput, serperStatus);

  const proxyLabel = labelEl('URL du proxy CORS (SEARCH_PROXY_URL)');
  const proxyInput = document.createElement('input');
  Object.assign(proxyInput.style, {
    width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
    borderRadius:'8px', padding:'8px', fontSize:'13px', boxSizing:'border-box'
  });
  proxyInput.type = 'text';
  proxyInput.placeholder = 'https://proxy.sicho95.workers.dev/';
  proxyInput.value = lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/';
  proxyInput.onchange = () => { lsSet('SEARCH_PROXY_URL', proxyInput.value.trim()); showToast('SEARCH_PROXY_URL sauvegardée.'); };
  searchCard.append(proxyLabel, proxyInput);
  list.appendChild(searchCard);

  // ── Section Bourse ─────────────────────────────────────────────────────────
  const bourseCard = el('div', { background:'#071407', border:'1px solid #1a3a1a', borderRadius:'10px', padding:'12px', marginTop:'4px' });
  const bTitle = el('div', { fontWeight:'600', fontSize:'13px', marginBottom:'4px' });
  bTitle.textContent = '📈 Bourse — Twelve Data';
  bourseCard.appendChild(bTitle);

  const bDesc = el('div', { fontSize:'11px', color:'#777', marginBottom:'10px', lineHeight:'1.5' });
  bDesc.innerHTML = 'Cours en temps réel (CAC40, BTC, EUR/USD, Or). Clé gratuite sur <a href="https://twelvedata.com" target="_blank" style="color:#5af">twelvedata.com</a>.';
  bourseCard.appendChild(bDesc);

  const tdLabel = labelEl('Clé Twelve Data (TWELVE_DATA_KEY)');
  const tdInput = document.createElement('input');
  tdInput.type = 'password'; tdInput.autocomplete = 'off';
  Object.assign(tdInput.style, {
    width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
    borderRadius:'8px', padding:'8px', fontSize:'13px', boxSizing:'border-box'
  });
  tdInput.placeholder = 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx';
  tdInput.value = lsGet('TWELVE_DATA_KEY') || '';
  tdInput.onchange = () => { lsSet('TWELVE_DATA_KEY', tdInput.value.trim()); showToast('TWELVE_DATA_KEY sauvegardée.'); };
  bourseCard.appendChild(tdLabel);
  bourseCard.appendChild(tdInput);

  const tdStatus = el('div', { fontSize:'11px', marginTop:'4px' });
  const hasTD = !!(lsGet('TWELVE_DATA_KEY') || '').trim();
  tdStatus.textContent = hasTD ? '✅ Clé présente' : '⚠️ Clé manquante — la vue Bourse affichera un message';
  tdStatus.style.color = hasTD ? '#5a9' : '#a66';
  bourseCard.appendChild(tdStatus);
  list.appendChild(bourseCard);

  // ── Section TTS ────────────────────────────────────────────────────────────
  const ttsCard = el('div', { background:'#0a0a1a', border:'1px solid #1a1a3a', borderRadius:'10px', padding:'12px', marginTop:'4px' });
  const tTitle = el('div', { fontWeight:'600', fontSize:'13px', marginBottom:'4px' });
  tTitle.textContent = '🔊 Text-to-Speech (TTS)';

  const ttsStatus = getTTSStatus();
  const ttsBadge = el('div', { fontSize:'11px', marginBottom:'10px', padding:'4px 8px', borderRadius:'6px',
    background: ttsStatus.engine === 'gemini' ? '#1a2a3a' : ttsStatus.engine === 'silent' ? '#2a1a1a' : '#1a1a2a',
    color: ttsStatus.engine === 'gemini' ? '#88f' : ttsStatus.engine === 'silent' ? '#f88' : '#aaf',
    display: 'inline-block' });
  const ttsEngineIcons = { gemini:'🌐', browser:'🗣', silent:'🔇', none:'❌' };
  ttsBadge.textContent = (ttsEngineIcons[ttsStatus.engine] || '') + ' ' + ttsStatus.reason;

  ttsCard.append(tTitle, ttsBadge);

  // Moteur
  const engLabel = labelEl('Moteur TTS');
  const engSel = document.createElement('select');
  Object.assign(engSel.style, { width:'100%', background:'#111', color:'#ccc', border:'1px solid #333', borderRadius:'8px', padding:'8px', fontSize:'13px' });
  [['browser', '🗣 Navigateur (offline, gratuit)'], ['gemini', '🌐 Gemini Cloud (voix naturelle)']].forEach(([val, lbl]) => {
    const opt = document.createElement('option'); opt.value = val; opt.textContent = lbl;
    if ((lsGet('NESTOR_TTS_ENGINE') || 'browser') === val) opt.selected = true;
    engSel.appendChild(opt);
  });
  engSel.onchange = () => { lsSet('NESTOR_TTS_ENGINE', engSel.value); showToast('Moteur TTS : ' + engSel.value); rerender(); };
  ttsCard.append(engLabel, engSel);

  // Voix (browser seulement)
  const voiceWrap = el('div', {});
  const voiceLabel = labelEl('Voix navigateur');
  const voiceSel = document.createElement('select');
  Object.assign(voiceSel.style, { width:'100%', background:'#111', color:'#ccc', border:'1px solid #333', borderRadius:'8px', padding:'8px', fontSize:'13px' });
  const savedVoice = lsGet('NESTOR_TTS_VOICE') || '';
  listBrowserVoices().then(voices => {
    const defOpt = document.createElement('option'); defOpt.value = ''; defOpt.textContent = '— Auto (voix française)'; voiceSel.appendChild(defOpt);
    voices.forEach(v => {
      const opt = document.createElement('option'); opt.value = v.name;
      opt.textContent = v.name + ' (' + v.lang + ')';
      if (v.name === savedVoice) opt.selected = true;
      voiceSel.appendChild(opt);
    });
  });
  voiceSel.onchange = () => { lsSet('NESTOR_TTS_VOICE', voiceSel.value); showToast('Voix TTS sauvegardée.'); };
  voiceWrap.append(voiceLabel, voiceSel);
  ttsCard.appendChild(voiceWrap);

  // Vitesse
  const rateLabel = labelEl('Vitesse de lecture');
  const rateRow = el('div', { display:'flex', alignItems:'center', gap:'10px' });
  const rateSlider = document.createElement('input');
  rateSlider.type = 'range'; rateSlider.min = '0.5'; rateSlider.max = '2.0'; rateSlider.step = '0.1';
  rateSlider.value = lsGet('NESTOR_TTS_RATE') || '1.0';
  rateSlider.style.flex = '1';
  const rateDisplay = el('div', { fontSize:'12px', color:'#888', minWidth:'30px', textAlign:'right' });
  rateDisplay.textContent = rateSlider.value + '×';
  rateSlider.oninput = () => { rateDisplay.textContent = rateSlider.value + '×'; lsSet('NESTOR_TTS_RATE', rateSlider.value); };
  rateRow.append(rateSlider, rateDisplay);
  ttsCard.append(rateLabel, rateRow);

  // Bouton test
  const testBtn = btn('▶ Tester la voix', '', async () => {
    testBtn.disabled = true; testBtn.textContent = '⏳ Lecture…';
    try {
      const { speak: spk } = await import('../api/tts.js');
      await spk('Bonjour, je suis Nestor, votre assistant personnel.');
    } catch(e) { showToast('Erreur TTS : ' + e.message, true); }
    testBtn.disabled = false; testBtn.textContent = '▶ Tester la voix';
  });
  testBtn.style.marginTop = '8px';
  ttsCard.appendChild(testBtn);

  // Mode silence
  const silentRow = el('div', { display:'flex', alignItems:'center', gap:'10px', marginTop:'8px' });
  const silentLabel = el('div', { fontSize:'12px', color:'#888', flex:'1' });
  silentLabel.textContent = '🔇 Mode silence global';
  const silentToggle = document.createElement('input');
  silentToggle.type = 'checkbox';
  silentToggle.checked = isSilentMode();
  silentToggle.onchange = () => {
    setSilentMode(silentToggle.checked);
    showToast(silentToggle.checked ? 'Silence activé.' : 'Voix réactivée.');
    rerender();
  };
  silentRow.append(silentLabel, silentToggle);
  ttsCard.appendChild(silentRow);
  list.appendChild(ttsCard);

  container.appendChild(list);
}

// ─── Utilitaires DOM ──────────────────────────────────────────────────────────
function el(tagName, styles) {
  const e = document.createElement(tagName);
  if (styles) Object.assign(e.style, styles);
  return e;
}
function btn(text, variant, onClick) {
  const b = document.createElement('button');
  b.textContent = text;
  Object.assign(b.style, {
    padding: variant === 'primary' ? '7px 14px' : '6px 12px',
    background: variant === 'primary' ? '#1a4a2a' : '#222',
    color: variant === 'primary' ? '#7ef' : '#ccc',
    border: '1px solid ' + (variant === 'primary' ? '#2a6a3a' : '#333'),
    borderRadius: '8px', fontSize: '13px', cursor:'pointer', whiteSpace:'nowrap',
  });
  b.onclick = onClick;
  return b;
}
function labelEl(text) {
  const l = el('div', { fontSize:'12px', color:'#888', marginBottom:'2px', marginTop:'6px' });
  l.textContent = text; return l;
}
function esc(str) {
  return String(str || '').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

let toastTimer;
function showToast(msg, isError) {
  let toast = document.getElementById('nestor-toast');
  if (!toast) {
    toast = document.createElement('div');
    toast.id = 'nestor-toast';
    Object.assign(toast.style, {
      position:'fixed', bottom:'24px', left:'50%', transform:'translateX(-50%)',
      padding:'10px 20px', borderRadius:'20px', fontSize:'13px',
      maxWidth:'80vw', textAlign:'center', zIndex:'9999',
      transition:'opacity 0.3s', pointerEvents:'none'
    });
    document.body.appendChild(toast);
  }
  toast.textContent = msg;
  toast.style.background = isError ? '#4a1a1a' : '#1a3a2a';
  toast.style.color = isError ? '#f88' : '#7ef';
  toast.style.border = '1px solid ' + (isError ? '#6a2a2a' : '#2a5a3a');
  toast.style.opacity = '1';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toast.style.opacity = '0'; }, 3000);
}
