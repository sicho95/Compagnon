import { listBackends, callLLM, loadGroqModels, resetGroqModelsCache } from '../api/backends.js';
import { saveAgent, deleteAgent, exportAgentsJson, downloadText, importAgentsJson, lsGet, lsSet } from '../storage/agents-db.js';
import { gardenerMerge } from '../core/gardener.js';
import { searchWeb, searchWebMulti, getSearchStatus } from '../api/search.js';
import { speak, stopSpeech, isSilentMode, setSilentMode, isSpeechEnabled, listBrowserVoices } from '../api/tts.js';
import { resolve as orchestratorResolve, ROLES } from '../core/orchestrator-engine.js';

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
  return {
    main: text.slice(0, idx).trimEnd(),
    sources: match[0].trim(),
  };
}

// ─── Texte pour TTS : retire les sources et les URLs ──────────────────────────
function textForTTS(text) {
  const { main } = splitReplyAndSources(text);
  return main.replace(/https?:\/\/\S+/g, '').replace(/\s{2,}/g, ' ').trim();
}

export function renderDashboard(container, state, rerender) {
  container.innerHTML = '';

  if (state.view === 'chat' && state.activeAgent) { renderChatView(container, state, rerender); return; }
  if (state.view === 'edit' && state.editingAgent) { renderEditView(container, state, rerender); return; }
  if (state.view === 'fabrique') { renderFabriqueView(container, state, rerender); return; }

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
      state.chatHistory = [{ role: 'system', content: agent.system_prompt || '' }];
      state.view = 'chat';
      rerender();
    });

    const btnEdit = btn('✏️ Éditer', '', () => {
      state.editingAgent = JSON.parse(JSON.stringify(agent));
      state.view = 'edit';
      rerender();
    });

    const btnDel = btn('🗑', '', async () => {
      if (!confirm('Supprimer ' + agent.name + ' ?')) return;
      await deleteAgent(agent.id);
      state.agents = state.agents.filter(a => a.id !== agent.id);
      rerender();
    });
    btnDel.style.marginLeft = 'auto';

    btnsRow.append(btnChat, btnEdit, btnDel);
    card.appendChild(btnsRow);
    list.appendChild(card);
  });

  container.appendChild(list);
}

// ─── Chat View ───────────────────────────────────────────────────────────────
function renderChatView(container, state, rerender) {
  const agent = state.activeAgent;
  const isOrchestrator = agent.role === ROLES.ORCHESTRATOR;
  state.chatHistory = state.chatHistory || [{ role:'system', content: agent.system_prompt || '' }];

  const header = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'10px' });
  const title = el('div', { fontWeight:'600', fontSize:'15px', flex:'1' });
  title.textContent = roleIcon(agent.role) + ' ' + agent.name;
  if (isOrchestrator) {
    const badge = tag('Moteur actif', '#1a3a2a');
    badge.style.color = '#7ef';
    title.appendChild(document.createTextNode(' '));
    title.appendChild(badge);
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
          const srcEl = el('div', {
            marginTop: '10px',
            paddingTop: '8px',
            borderTop: '1px solid #2a2a3a',
            fontSize: '10px',
            color: '#556',
            lineHeight: '1.5',
            whiteSpace: 'pre-wrap',
          });
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
    const txt = feedbackInput.value.trim();
    if (!txt) return;
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
  input.placeholder = 'Ton message… (clavier ou dictée)';
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

    if (reply && !isSilentMode()) {
      const ttsText = textForTTS(reply);
      if (ttsText) speak(ttsText).catch(e => console.warn('[Nestor/TTS]', e.message));
    }
  }

  inputRow.append(input, sendBtn);
  container.appendChild(inputRow);
}

// ─── Découpage de requête complexe en sous-requêtes ──────────────────────────
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

// ─── Fabrique View ───────────────────────────────────────────────────────────
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
      const previewTitle = el('div', { fontWeight:'600', fontSize:'13px', marginBottom:'6px', color:'#9ef' });
      previewTitle.textContent = '👁 Aperçu : ' + newAgent.name + ' (' + newAgent.role + ')';
      const previewDesc = el('div', { fontSize:'12px', color:'#888', marginBottom:'6px' });
      previewDesc.textContent = newAgent.description || '';
      const previewTags = el('div', { display:'flex', flexWrap:'wrap', gap:'4px', marginBottom:'8px' });
      (newAgent.tags || []).forEach(t => previewTags.appendChild(tag(t)));
      previewZone.append(previewTitle, previewDesc, previewTags);

      const saveBtn = btn('✅ Sauvegarder cet agent', 'primary', async () => {
        await saveAgent(newAgent);
        state.agents = state.agents.concat([newAgent]);
        showToast('Agent "' + newAgent.name + '" créé !');
        state.view = 'agents';
        rerender();
      });
      previewZone.appendChild(saveBtn);
    } catch(e) {
      showToast('Erreur Fabrique : ' + e.message, true);
    }
    genBtn.textContent = '✨ Générer avec la Fabrique';
    genBtn.disabled = false;
  });
  container.appendChild(genBtn);

  const cancelBtn = btn('← Retour', '', () => { state.view = 'agents'; rerender(); });
  cancelBtn.style.marginTop = '8px';
  container.appendChild(cancelBtn);
}

// ─── Edit View ───────────────────────────────────────────────────────────────
function renderEditView(container, state, rerender) {
  const agent = state.editingAgent;

  const title = el('div', { fontWeight:'600', fontSize:'15px', marginBottom:'12px' });
  title.textContent = '✏️ Éditer : ' + agent.name;
  container.appendChild(title);

  const fields = [
    { key:'name',          label:'Nom',           type:'input'    },
    { key:'description',   label:'Description',   type:'textarea' },
    { key:'system_prompt', label:'System Prompt', type:'textarea', rows:6 },
    { key:'tags',          label:'Tags (virgule)',type:'input'    },
  ];

  fields.forEach(({ key, label, type, rows }) => {
    container.appendChild(labelEl(label));
    let el2;
    if (type === 'textarea') {
      el2 = document.createElement('textarea');
      el2.rows = rows || 3;
    } else {
      el2 = document.createElement('input');
    }
    Object.assign(el2.style, {
      width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
      borderRadius:'8px', padding:'8px', fontSize:'13px', boxSizing:'border-box', marginBottom:'8px'
    });
    el2.value = key === 'tags'
      ? (agent.tags || []).join(', ')
      : (agent[key] || '');
    el2.addEventListener('input', () => {
      agent[key] = key === 'tags'
        ? el2.value.split(',').map(s => s.trim()).filter(Boolean)
        : el2.value;
    });
    container.appendChild(el2);
  });

  container.appendChild(labelEl('Backend LLM'));
  const backendSel = document.createElement('select');
  Object.assign(backendSel.style, {
    width:'100%', background:'#111', color:'#ccc', border:'1px solid #333',
    borderRadius:'8px', padding:'8px', fontSize:'13px', marginBottom:'10px'
  });
  listBackends().forEach(b => {
    const opt = document.createElement('option');
    opt.value = b.id; opt.textContent = b.label;
    if (b.id === (agent.backendId || 'groq-llama')) opt.selected = true;
    backendSel.appendChild(opt);
  });
  backendSel.addEventListener('change', () => { agent.backendId = backendSel.value; });
  container.appendChild(backendSel);

  const btns = el('div', { display:'flex', gap:'8px', marginTop:'8px' });
  const saveBtn = btn('💾 Sauvegarder', 'primary', async () => {
    agent.tags = Array.isArray(agent.tags) ? agent.tags : (agent.tags || '').split(',').map(s=>s.trim()).filter(Boolean);
    agent.updatedAt = new Date().toISOString();
    await saveAgent(agent);
    state.agents = state.agents.map(a => a.id === agent.id ? agent : a);
    showToast('Agent "' + agent.name + '" sauvegardé.');
    state.view = 'agents';
    rerender();
  });
  const cancelBtn = btn('← Annuler', '', () => { state.view = 'agents'; rerender(); });
  btns.append(saveBtn, cancelBtn);
  container.appendChild(btns);
}

// ─── Settings View ────────────────────────────────────────────────────────────
function renderSettings(container, state, rerender) {
  // NB : le titre "⚙️ Réglages" est déjà affiché par renderDashboard — pas de doublon ici.

  // ── Section : Clés API ────────────────────────────────────────────────────
  const sectionApi = el('div', {
    background:'#111', border:'1px solid #2a2a2a', borderRadius:'10px',
    padding:'12px', marginBottom:'12px', display:'flex', flexDirection:'column', gap:'10px'
  });

  const sectionApiTitle = el('div', { fontSize:'11px', color:'#5a9', fontWeight:'600',
    textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'2px' });
  sectionApiTitle.textContent = '🔑 Clés API';
  sectionApi.appendChild(sectionApiTitle);

  // Helper : champ clé API
  function apiKeyField(label, storageKey, placeholder) {
    const wrap = el('div', { display:'flex', flexDirection:'column', gap:'3px' });
    wrap.appendChild(labelEl(label));
    const inp = document.createElement('input');
    inp.type = 'password';
    inp.placeholder = placeholder || 'sk-…';
    inp.value = lsGet(storageKey) || '';
    inp.setAttribute('autocomplete', 'off');
    Object.assign(inp.style, {
      width:'100%', background:'#0d0d0d', color:'#ccc', border:'1px solid #333',
      borderRadius:'8px', padding:'8px 10px', fontSize:'13px', boxSizing:'border-box',
    });

    const row = el('div', { display:'flex', gap:'6px', alignItems:'center' });
    row.appendChild(inp);

    // Bouton afficher/masquer
    const toggleBtn = btn('👁', '', () => {
      inp.type = inp.type === 'password' ? 'text' : 'password';
    });
    Object.assign(toggleBtn.style, { padding:'4px 8px', fontSize:'14px', flexShrink:'0' });
    row.appendChild(toggleBtn);

    // Bouton sauvegarder
    const saveBtn = btn('✔', 'primary', () => {
      const val = inp.value.trim();
      lsSet(storageKey, val);
      showToast(label + ' sauvegardée.');
    });
    Object.assign(saveBtn.style, { padding:'4px 10px', flexShrink:'0' });
    row.appendChild(saveBtn);

    wrap.appendChild(row);
    return wrap;
  }

  sectionApi.appendChild(apiKeyField('Groq API Key', 'GROQ_API_KEY', 'gsk_…'));
  sectionApi.appendChild(apiKeyField('Perplexity API Key', 'PERPLEXITY_API_KEY', 'pplx-…'));
  container.appendChild(sectionApi);

  // ── Section : Voix TTS ────────────────────────────────────────────────────
  const voices = listBrowserVoices();
  if (voices.length > 0) {
    const sectionTts = el('div', {
      background:'#111', border:'1px solid #2a2a2a', borderRadius:'10px',
      padding:'12px', marginBottom:'12px'
    });
    const sectionTtsTitle = el('div', { fontSize:'11px', color:'#5a9', fontWeight:'600',
      textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'8px' });
    sectionTtsTitle.textContent = '🔊 Voix TTS';
    sectionTts.appendChild(sectionTtsTitle);

    const sel = document.createElement('select');
    Object.assign(sel.style, {
      width:'100%', background:'#0d0d0d', color:'#ccc', border:'1px solid #333',
      borderRadius:'8px', padding:'8px', fontSize:'13px',
    });
    voices.forEach((v, i) => {
      const opt = document.createElement('option');
      opt.value = i; opt.textContent = v.name + ' (' + v.lang + ')';
      sel.appendChild(opt);
    });
    sel.addEventListener('change', () => {
      lsSet('nestor_voice_index', sel.value);
      showToast('Voix : ' + voices[sel.value]?.name);
    });
    const saved = lsGet('nestor_voice_index');
    if (saved !== null) sel.value = saved;
    sectionTts.appendChild(sel);
    container.appendChild(sectionTts);
  }

  // ── Section : Modèles Groq ────────────────────────────────────────────────
  const sectionGroq = el('div', {
    background:'#111', border:'1px solid #2a2a2a', borderRadius:'10px',
    padding:'12px', marginBottom:'12px'
  });
  const sectionGroqTitle = el('div', { fontSize:'11px', color:'#5a9', fontWeight:'600',
    textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'8px' });
  sectionGroqTitle.textContent = '🤖 Modèles Groq';
  sectionGroq.appendChild(sectionGroqTitle);

  const groqHint = el('div', { fontSize:'11px', color:'#666', marginBottom:'8px' });
  groqHint.textContent = 'Recharge la liste des modèles Groq disponibles (requiert la clé Groq).';
  sectionGroq.appendChild(groqHint);

  const loadModelsBtn = btn('🔄 Charger modèles Groq', '', async () => {
    loadModelsBtn.textContent = '⏳ Chargement…';
    loadModelsBtn.disabled = true;
    try {
      await resetGroqModelsCache();
      const backends = listBackends().filter(b => b.groqDynamic);
      showToast(backends.length + ' modèles Groq chargés.');
      rerender();
    } catch(e) {
      showToast('Erreur : ' + e.message, true);
    }
    loadModelsBtn.textContent = '🔄 Charger modèles Groq';
    loadModelsBtn.disabled = false;
  });
  Object.assign(loadModelsBtn.style, { width:'100%' });
  sectionGroq.appendChild(loadModelsBtn);
  container.appendChild(sectionGroq);

  // ── Retour ────────────────────────────────────────────────────────────────
  const backBtn = btn('← Retour', '', () => { state.view = 'agents'; rerender(); });
  Object.assign(backBtn.style, { marginTop:'4px', width:'100%' });
  container.appendChild(backBtn);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
function el(tag, styles = {}) {
  const e = document.createElement(tag);
  Object.assign(e.style, styles);
  return e;
}

function btn(text, variant, onClick) {
  const b = document.createElement('button');
  b.textContent = text;
  const isPrimary = variant === 'primary';
  Object.assign(b.style, {
    padding: '6px 12px', borderRadius: '8px', fontSize: '13px', cursor: 'pointer',
    border: isPrimary ? 'none' : '1px solid #333',
    background: isPrimary ? '#1a4a2a' : '#1a1a1a',
    color: isPrimary ? '#7ef' : '#ccc',
    transition: 'background 0.15s',
  });
  b.addEventListener('mouseenter', () => { b.style.background = isPrimary ? '#1f5a32' : '#252525'; });
  b.addEventListener('mouseleave', () => { b.style.background = isPrimary ? '#1a4a2a' : '#1a1a1a'; });
  b.addEventListener('click', onClick);
  return b;
}

function labelEl(text) {
  const l = document.createElement('label');
  l.textContent = text;
  Object.assign(l.style, { fontSize:'11px', color:'#666', display:'block', marginBottom:'3px' });
  return l;
}

function showToast(msg, isError = false) {
  const t = document.createElement('div');
  t.textContent = msg;
  Object.assign(t.style, {
    position:'fixed', bottom:'20px', left:'50%', transform:'translateX(-50%)',
    background: isError ? '#3a1a1a' : '#1a3a1a',
    color: isError ? '#f88' : '#7ef',
    padding:'8px 16px', borderRadius:'8px', fontSize:'13px',
    zIndex:'9999', pointerEvents:'none',
    animation:'fadeInOut 2.5s ease forwards',
  });
  document.body.appendChild(t);
  setTimeout(() => t.remove(), 2600);
}
