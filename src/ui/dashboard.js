import { listBackends, callLLM, loadGroqModels, resetGroqModelsCache } from '../api/backends.js';
import { saveAgent, deleteAgent, exportAgentsJson, downloadText, importAgentsJson,
         lsGet, lsSet, saveChatHistory, loadChatHistory, clearChatHistory } from '../storage/agents-db.js';
import { deviceStatus } from '../bt/ble_status.js';
import { scanWifiNetworks, provisionWifi } from '../device/provisioning.js';
import { gardenerMerge } from '../core/gardener.js';
import { searchWeb, searchWebMulti, getSearchStatus } from '../api/search.js';
import { speak, stopSpeech, isSilentMode, setSilentMode, isSpeechEnabled,
         listBrowserVoices, getTTSStatus, GEMINI_VOICES } from '../api/tts.js';
import { getSTTStatus } from '../api/stt.js';
import { resolve as orchestratorResolve, ROLES } from '../core/orchestrator-engine.js';
import { renderRadarView, cleanupRadarView } from './radar-view.js';
import { renderBourseView, cleanupBourseView } from './bourse-view.js';
import { renderMeteoView as renderMeteoContent } from './meteo-view.js';
import { renderMusiqueView as renderMusiqueContent, cleanupMusiqueView } from './musique-view.js';
import { getSettings, setSettings, getNestorSettings, getBourseSettings,
         getMeteoSettings, getMusiqueSettings, getDomotiqueSettings,
         setNestorSettings, setBourseSettings,
         setMeteoSettings, setMusiqueSettings, setDomotiqueSettings } from '../core/settings-store.js';
import { syncApiKeys, fetchDeviceKeyStatus } from '../sync/key-sync.js';
import { bleConnected } from '../bt/ble.js';

const ROLE_ICONS = {
  orchestrator: '\uD83E\uDDE0', gardener: '\uD83C\uDF3F', factory: '\uD83C\uDFED',
  'monthly-payments': '\uD83D\uDCC5', 'pea-portfolio': '\uD83D\uDCC8', stories: '\uD83D\uDCDA',
  research: '\uD83D\uDD0D', 'web-search': '\uD83C\uDF10', 'web-analyst': '\uD83D\uDD0E', generic: '\uD83E\uDD16',
  maison: '\uD83C\uDFE0',
};
function roleIcon(role) { return ROLE_ICONS[role] || '\uD83E\uDD16'; }
function tag(text, color) {
  const s = document.createElement('span');
  s.textContent = text;
  Object.assign(s.style, { fontSize:'10px', padding:'2px 6px', borderRadius:'10px',
    background: color || '#2a2a2a', color:'#ccc', display:'inline-block', marginRight:'4px' });
  return s;
}

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

  if (state.view === 'hub')    { renderHubView(container, state, rerender); return; }
  if (state.view === 'chat' && state.activeAgent) { renderChatView(container, state, rerender); return; }
  if (state.view === 'edit' && state.editingAgent) { renderEditView(container, state, rerender); return; }
  if (state.view === 'fabrique') { renderFabriqueView(container, state, rerender); return; }
  if (state.view === 'radar')  { renderRadarSection(container, state, rerender); return; }
  if (state.view === 'bourse') { renderBourseSection(container, state, rerender); return; }
  if (state.view === 'meteo')  { renderMeteoSection(container, state, rerender); return; }
  if (state.view === 'musique'){ renderMusiqueSection(container, state, rerender); return; }
  if (state.view === 'companion'){ renderCompanionSection(container, state, rerender); return; }

  const header = el('div', { display:'flex', justifyContent:'space-between', alignItems:'center', marginBottom:'10px' });
  const leftRow = el('div', { display:'flex', alignItems:'center', gap:'8px' });
  const backBtn = btn('\u2190 Hub', '', () => { state.view = 'hub'; rerender(); });
  leftRow.appendChild(backBtn);
  header.appendChild(leftRow);

  if (state.view === 'agents') {
    const addBtn = btn('+ Nouvel agent', 'primary', () => {
      state.editingAgent = {
        id: 'agent-' + Date.now(),
        name: 'Nouvel agent',
        role: 'generic',
        description: '',
        tags: [],
        backendId: 'groq-llama',
        system_prompt: '',
        memory_profile: { level: 'normal', scope: 'session' },
        preferences: [],
        examples: [],
        metrics: { corrections: 0, confidence: 1, lastUsed: null },
        version: 1,
        createdAt: new Date().toISOString(),
        updatedAt: new Date().toISOString(),
      };
      state.view = 'edit';
      rerender();
    });
    header.appendChild(addBtn);
  }

  container.appendChild(header);

  if (state.view === 'agents') { renderAgentsList(container, state, rerender); return; }
  if (state.view === 'settings') { renderSettings(container, state, rerender); return; }

  const msg = el('div', { color:'#666', textAlign:'center', padding:'40px 0' });
  msg.textContent = 'Vue inconnue : ' + state.view;
  container.appendChild(msg);
}

// ─── Radar ────────────────────────────────────────────────────────────────────
function renderRadarSection(container, state, rerender) {
  const backBtn = btn('\u2190 Hub', '', () => {
    cleanupRadarView();
    state.view = state._radarPrevView || 'hub';
    rerender();
  });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);
  const body = el('div', {});
  container.appendChild(body);
  renderRadarView(body, state, rerender, () => { state.view = state._radarPrevView || 'hub'; rerender(); });
}

// ─── Bourse ───────────────────────────────────────────────────────────────────
function renderBourseSection(container, state, rerender) {
  const backBtn = btn('\u2190 Hub', '', () => {
    cleanupBourseView(container.querySelector('[data-bourse]'));
    state.view = state._boursePrevView || 'hub';
    rerender();
  });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);
  const body = el('div', {});
  body.setAttribute('data-bourse', '1');
  container.appendChild(body);
  renderBourseView(body, state, rerender);
}

// ─── Météo ────────────────────────────────────────────────────────────────────
function renderMeteoSection(container, state, rerender) {
  const backBtn = btn('\u2190 Hub', '', () => { state.view = state._meteoPrevView || 'hub'; rerender(); });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);
  const body = el('div', {});
  container.appendChild(body);
  renderMeteoContent(body, state, rerender);
}

// ─── Musique ──────────────────────────────────────────────────────────────────
function renderMusiqueSection(container, state, rerender) {
  const backBtn = btn('\u2190 Hub', '', () => {
    cleanupMusiqueView();
    state.view = state._musiquePrevView || 'hub';
    rerender();
  });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);
  const body = el('div', {});
  container.appendChild(body);
  renderMusiqueContent(body, state, rerender);
}

// ─── Companion ESP32 ──────────────────────────────────────────────────────────
function renderCompanionSection(container, state, rerender) {
  const backBtn = btn('\u2190 Hub', '', () => { state.view = 'hub'; rerender(); });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);
  const body = el('div', {});
  container.appendChild(body);
  import('./companion.js').then(({ renderCompanionView }) => {
    renderCompanionView(body, state, rerender);
  }).catch(() => {
    body.textContent = '\u26a0\ufe0f Module Companion ESP32 non disponible.';
  });
}

// ═══════════════════════════════════════════════════════════════════════════════
// HUB VIEW
// ═══════════════════════════════════════════════════════════════════════════════
function renderHubView(container, state, rerender) {
  container.style.cssText = 'display:flex;flex-direction:column;gap:10px;padding:4px 0;';

  const titleRow = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'4px' });
  const titleText = el('div', { fontSize:'15px', fontWeight:'700', color:'#eee', flex:'1' });
  titleText.textContent = '\uD83C\uDFE0 Compagnon';
  titleRow.appendChild(titleText);

  const bleStatus = el('div', { fontSize:'11px', padding:'2px 8px', borderRadius:'10px',
    background: deviceStatus.connected ? '#1a3a1a' : '#2a1a1a',
    color: deviceStatus.connected ? '#5ef' : '#e65', whiteSpace:'nowrap' });
  bleStatus.textContent = deviceStatus.connected ? '\u25cf ESP32 connect\u00e9' : '\u25cb ESP32 d\u00e9connect\u00e9';
  titleRow.appendChild(bleStatus);
  container.appendChild(titleRow);

  const grid = el('div', { display:'grid', gridTemplateColumns:'repeat(3, 1fr)', gap:'8px' });

  const appCards = [
    { icon:'\uD83E\uDDE0', label:'Nestor', view:'chat', action: () => {
        const orch = state.agents.find(a => a.role === 'orchestrator');
        if (orch) {
          state.activeAgent = orch;
          const hist = loadChatHistory(orch.id);
          state.chatHistory = [{ role:'system', content: orch.system_prompt || '' }, ...hist];
        }
        state.view = 'chat'; rerender();
      }},
    { icon:'\uD83D\uDEA8', label:'Radars', view:'radar', action: () => { state._radarPrevView = 'hub'; state.view = 'radar'; rerender(); }},
    { icon:'\uD83C\uDF24', label:'M\u00e9t\u00e9o', view:'meteo', action: () => { state._meteoPrevView = 'hub'; state.view = 'meteo'; rerender(); }},
    { icon:'\uD83D\uDCC8', label:'Bourse', view:'bourse', action: () => { state._boursePrevView = 'hub'; state.view = 'bourse'; rerender(); }},
    { icon:'\uD83C\uDFB5', label:'Musique', view:'musique', action: () => { state._musiquePrevView = 'hub'; state.view = 'musique'; rerender(); }},
    { icon:'\uD83D\uDCF1', label:'ESP32', view:'companion', action: () => { state.view = 'companion'; rerender(); }},
  ];

  appCards.forEach(({ icon, label, view, action }) => {
    const card = el('button', {
      background: state.view === view ? '#1a2a2a' : '#111',
      border: '1px solid ' + (state.view === view ? '#3a7a7a' : '#222'),
      borderRadius: '10px',
      padding: '14px 8px',
      cursor: 'pointer',
      display: 'flex',
      flexDirection: 'column',
      alignItems: 'center',
      gap: '6px',
      color: state.view === view ? '#5ef' : '#bbb',
      transition: 'background 0.15s',
      WebkitTapHighlightColor: 'transparent',
    });
    const iconEl = el('span', { fontSize:'22px', lineHeight:'1' }); iconEl.textContent = icon;
    const labelEl = el('span', { fontSize:'11px', fontWeight:'500' }); labelEl.textContent = label;
    card.appendChild(iconEl); card.appendChild(labelEl);
    card.onclick = action;
    grid.appendChild(card);
  });

  container.appendChild(grid);

  const activeAgents = state.agents.filter(a =>
    a.role !== 'orchestrator' && a.role !== 'gardener' && a.role !== 'factory'
  ).slice(0, 4);

  if (activeAgents.length > 0) {
    const sectionLabel = el('div', { fontSize:'11px', color:'#555', textTransform:'uppercase', letterSpacing:'0.06em', marginTop:'8px', marginBottom:'4px' });
    sectionLabel.textContent = 'Agents r\u00e9cents';
    container.appendChild(sectionLabel);
    const agentList = el('div', { display:'flex', flexDirection:'column', gap:'6px' });
    activeAgents.forEach(agent => {
      const row = el('button', {
        display:'flex', alignItems:'center', gap:'10px',
        background:'#111', border:'1px solid #1e1e1e', borderRadius:'8px',
        padding:'8px 12px', cursor:'pointer', color:'#bbb', fontSize:'13px',
        width:'100%', textAlign:'left', WebkitTapHighlightColor:'transparent',
      });
      const ic = el('span', { fontSize:'16px' }); ic.textContent = roleIcon(agent.role);
      const nm = el('span', { flex:'1', fontWeight:'500', color:'#ddd', overflow:'hidden', textOverflow:'ellipsis', whiteSpace:'nowrap' });
      nm.textContent = agent.name;
      const arr = el('span', { color:'#444', fontSize:'12px' }); arr.textContent = '\u203a';
      row.appendChild(ic); row.appendChild(nm); row.appendChild(arr);
      row.onclick = () => {
        state.activeAgent = agent;
        state.chatHistory = [{ role:'system', content: agent.system_prompt || '' }];
        state.view = 'chat'; rerender();
      };
      agentList.appendChild(row);
    });
    container.appendChild(agentList);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// CHAT VIEW
// ═══════════════════════════════════════════════════════════════════════════════
let _currentSTT = null;

function renderChatView(container, state, rerender) {
  const agent = state.activeAgent;
  const wrap = el('div', { display:'flex', flexDirection:'column', height:'100%', gap:'0' });

  const agentHeader = el('div', {
    display:'flex', alignItems:'center', gap:'8px',
    padding:'6px 0 8px', borderBottom:'1px solid #1a1a1a', marginBottom:'8px',
  });
  const agentIcon = el('span', { fontSize:'18px' }); agentIcon.textContent = roleIcon(agent.role);
  const agentName = el('span', { fontSize:'13px', fontWeight:'600', color:'#ccc', flex:'1' });
  agentName.textContent = agent.name;
  const clearBtn = btn('\uD83D\uDDD1', '', () => {
    state.chatHistory = [{ role:'system', content: agent.system_prompt || '' }];
    clearChatHistory(agent.id);
    rerender();
  });
  clearBtn.title = 'Effacer l\'historique';
  clearBtn.style.cssText = 'background:none;border:none;color:#555;padding:4px 6px;cursor:pointer;font-size:14px;';
  agentHeader.appendChild(agentIcon); agentHeader.appendChild(agentName); agentHeader.appendChild(clearBtn);
  wrap.appendChild(agentHeader);

  const messages = el('div', {
    flex:'1', overflowY:'auto', display:'flex', flexDirection:'column',
    gap:'8px', paddingBottom:'8px',
  });

  const chatMessages = state.chatHistory.filter(m => m.role !== 'system');
  if (chatMessages.length === 0) {
    const empty = el('div', { textAlign:'center', color:'#444', fontSize:'12px', padding:'20px 0' });
    empty.textContent = 'D\u00e9marrez la conversation\u2026';
    messages.appendChild(empty);
  } else {
    chatMessages.forEach(m => {
      const bubble = el('div', {
        maxWidth:'88%', padding:'8px 10px', borderRadius:'10px', fontSize:'13px',
        lineHeight:'1.5', wordBreak:'break-word',
        alignSelf: m.role === 'user' ? 'flex-end' : 'flex-start',
        background: m.role === 'user' ? '#1a3a2a' : '#161616',
        color: m.role === 'user' ? '#8ef' : '#ccc',
        borderBottomRightRadius: m.role === 'user' ? '2px' : '10px',
        borderBottomLeftRadius: m.role === 'user' ? '10px' : '2px',
      });
      if (m.role === 'assistant') {
        const { main, sources } = splitReplyAndSources(m.content || '');
        bubble.innerHTML = formatMessage(main);
        if (sources) {
          const src = el('div', { fontSize:'11px', color:'#555', marginTop:'6px', borderTop:'1px solid #222', paddingTop:'4px' });
          src.textContent = sources;
          bubble.appendChild(src);
        }
        const ttsBtn = document.createElement('button');
        ttsBtn.style.cssText = 'background:none;border:none;color:#3a5a4a;padding:2px 4px;cursor:pointer;font-size:11px;display:block;margin-top:4px;';
        ttsBtn.textContent = '\uD83D\uDD0A';
        ttsBtn.onclick = () => speak(textForTTS(m.content || ''));
        bubble.appendChild(ttsBtn);
      } else {
        bubble.textContent = m.content || '';
      }
      messages.appendChild(bubble);
    });
  }

  if (state._thinking) {
    const thinking = el('div', {
      alignSelf:'flex-start', padding:'8px 12px', borderRadius:'10px',
      background:'#161616', color:'#555', fontSize:'12px',
    });
    thinking.textContent = '\u2026';
    messages.appendChild(thinking);
  }
  wrap.appendChild(messages);

  const inputRow = el('div', {
    display:'flex', gap:'6px', alignItems:'flex-end',
    borderTop:'1px solid #1a1a1a', paddingTop:'8px',
  });

  const textarea = document.createElement('textarea');
  textarea.placeholder = 'Message\u2026';
  textarea.rows = 1;
  Object.assign(textarea.style, {
    flex:'1', background:'#111', border:'1px solid #252525', borderRadius:'8px',
    color:'#eee', padding:'8px 10px', fontSize:'13px', resize:'none',
    lineHeight:'1.4', maxHeight:'80px', overflowY:'auto',
    fontFamily:'inherit', outline:'none',
  });
  textarea.addEventListener('input', () => {
    textarea.style.height = 'auto';
    textarea.style.height = Math.min(textarea.scrollHeight, 80) + 'px';
  });
  textarea.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMessage(); }
  });

  const sttStatus = getSTTStatus();
  if (sttStatus.available) {
    const sttBtn = document.createElement('button');
    sttBtn.style.cssText = 'background:#111;border:1px solid #252525;border-radius:8px;color:#888;padding:8px;cursor:pointer;font-size:16px;min-width:36px;';
    sttBtn.textContent = '\uD83C\uDF99';
    sttBtn.setAttribute('aria-label', 'Dict\u00e9e vocale');
    sttBtn.onclick = () => {
      if (_currentSTT) { _currentSTT.stop(); _currentSTT = null; sttBtn.textContent = '\uD83C\uDF99'; return; }
      const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
      if (!SpeechRecognition) return;
      const rec = new SpeechRecognition();
      rec.lang = 'fr-FR'; rec.interimResults = false; rec.maxAlternatives = 1;
      rec.onresult = (e) => {
        const transcript = e.results[0][0].transcript;
        textarea.value += (textarea.value ? ' ' : '') + transcript;
        textarea.dispatchEvent(new Event('input'));
      };
      rec.onend = () => { _currentSTT = null; sttBtn.textContent = '\uD83C\uDF99'; };
      rec.onerror = () => { _currentSTT = null; sttBtn.textContent = '\uD83C\uDF99'; };
      _currentSTT = rec;
      rec.start();
      sttBtn.textContent = '\u23f9';
    };
    inputRow.appendChild(sttBtn);
  }

  const sendBtn = document.createElement('button');
  sendBtn.textContent = '\u2192';
  sendBtn.style.cssText = 'background:#1a4a2a;border:none;border-radius:8px;color:#5ef;padding:8px 14px;cursor:pointer;font-size:15px;font-weight:700;min-width:40px;';
  sendBtn.setAttribute('aria-label', 'Envoyer');

  const sendMessage = async () => {
    const text = textarea.value.trim();
    if (!text || state._thinking) return;
    textarea.value = '';
    textarea.style.height = 'auto';
    state.chatHistory.push({ role:'user', content: text });
    state._thinking = true;
    rerender();
    try {
      let assistantText;
      if (agent.role === 'orchestrator') {
        const result = await orchestratorResolve(text, state.agents, agent);
        assistantText = (typeof result === 'string') ? result : (result?.reply || JSON.stringify(result));
      } else {
        const r = await callLLM(agent.backendId || 'groq-llama', {
          messages: state.chatHistory,
          agentConfig: agent,
        });
        assistantText = r?.message?.content || '(pas de r\u00e9ponse)';
      }
      state.chatHistory.push({ role:'assistant', content: assistantText });
      saveChatHistory(agent.id, state.chatHistory.filter(m => m.role !== 'system'));
      const ttsStatus = getTTSStatus();
      if (ttsStatus.enabled && !isSilentMode()) speak(textForTTS(assistantText));
    } catch (e) {
      state.chatHistory.push({ role:'assistant', content: '\u26a0\ufe0f Erreur : ' + e.message });
    } finally {
      state._thinking = false;
      rerender();
    }
  };

  sendBtn.onclick = sendMessage;
  inputRow.appendChild(textarea);
  inputRow.appendChild(sendBtn);
  wrap.appendChild(inputRow);
  container.appendChild(wrap);

  requestAnimationFrame(() => { messages.scrollTop = messages.scrollHeight; textarea.focus(); });
}

function formatMessage(text) {
  return text
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/```([\s\S]*?)```/g, '<pre style="background:#0a0a0a;padding:8px;border-radius:6px;overflow-x:auto;font-size:11px;margin:4px 0;white-space:pre-wrap;">$1</pre>')
    .replace(/`([^`]+)`/g, '<code style="background:#0a0a0a;padding:1px 4px;border-radius:3px;font-size:12px;">$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/\*([^*]+)\*/g, '<em>$1</em>')
    .replace(/^#{1,3}\s+(.+)$/gm, '<strong style="display:block;margin-top:6px;color:#ddd;">$1</strong>')
    .replace(/^[-\u2022]\s+(.+)$/gm, '<div style="padding-left:12px">\u2022 $1</div>')
    .replace(/\n/g, '<br>');
}

// ═══════════════════════════════════════════════════════════════════════════════
// AGENTS LIST
// ═══════════════════════════════════════════════════════════════════════════════
function renderAgentsList(container, state, rerender) {
  const agents = state.agents || [];
  if (agents.length === 0) {
    const empty = el('div', { textAlign:'center', color:'#555', padding:'30px 0' });
    empty.textContent = 'Aucun agent configur\u00e9.';
    container.appendChild(empty);
    return;
  }
  agents.forEach(agent => {
    const row = el('div', {
      display:'flex', alignItems:'center', gap:'10px',
      padding:'10px 12px', background:'#111', border:'1px solid #1e1e1e',
      borderRadius:'8px', marginBottom:'6px', cursor:'pointer',
    });
    const icon = el('span', { fontSize:'18px' }); icon.textContent = roleIcon(agent.role);
    const info = el('div', { flex:'1', overflow:'hidden' });
    const name = el('div', { fontSize:'13px', fontWeight:'600', color:'#ddd', overflow:'hidden', textOverflow:'ellipsis', whiteSpace:'nowrap' });
    name.textContent = agent.name;
    const desc = el('div', { fontSize:'11px', color:'#555', overflow:'hidden', textOverflow:'ellipsis', whiteSpace:'nowrap' });
    desc.textContent = agent.description || agent.role;
    info.appendChild(name); info.appendChild(desc);
    const editBtn = btn('\u270f\ufe0f', '', (e) => {
      e.stopPropagation();
      state.editingAgent = { ...agent }; state.view = 'edit'; rerender();
    });
    editBtn.style.cssText = 'background:none;border:none;color:#555;cursor:pointer;padding:4px 6px;font-size:14px;';
    const delBtn = btn('\uD83D\uDDD1', '', async (e) => {
      e.stopPropagation();
      if (!confirm('Supprimer l\'agent "' + agent.name + '" ?')) return;
      await deleteAgent(agent.id);
      state.agents = state.agents.filter(a => a.id !== agent.id);
      rerender();
    });
    delBtn.style.cssText = 'background:none;border:none;color:#4a2a2a;cursor:pointer;padding:4px 6px;font-size:14px;';
    row.appendChild(icon); row.appendChild(info); row.appendChild(editBtn); row.appendChild(delBtn);
    row.onclick = () => {
      state.activeAgent = agent;
      state.chatHistory = [{ role:'system', content: agent.system_prompt || '' }];
      state.view = 'chat'; rerender();
    };
    container.appendChild(row);
  });

  const ioRow = el('div', { display:'flex', gap:'8px', marginTop:'12px' });
  const exportBtn = btn('\u2b07 Export JSON', '', () => {
    downloadText(exportAgentsJson(state.agents), 'nestor-agents.json', 'application/json');
  });
  exportBtn.style.flex = '1';
  const importBtn = btn('\u2b06 Import JSON', '', async () => {
    const input = document.createElement('input');
    input.type = 'file'; input.accept = '.json';
    input.onchange = async (e) => {
      const file = e.target.files[0]; if (!file) return;
      const text = await file.text();
      const result = await importAgentsJson(text, state.agents);
      state.agents = result;
      showToast('Agents import\u00e9s : ' + result.length);
      rerender();
    };
    input.click();
  });
  importBtn.style.flex = '1';
  ioRow.appendChild(exportBtn); ioRow.appendChild(importBtn);
  container.appendChild(ioRow);
}

// ═══════════════════════════════════════════════════════════════════════════════
// EDIT VIEW
// ═══════════════════════════════════════════════════════════════════════════════
function renderEditView(container, state, rerender) {
  const agent = state.editingAgent;
  const isNew = !state.agents.find(a => a.id === agent.id);
  const form = el('div', { display:'flex', flexDirection:'column', gap:'10px' });

  form.appendChild(field('Nom', 'text', agent.name, v => { agent.name = v; }));

  const roleWrap = el('div', {});
  const roleLbl = el('label', { fontSize:'11px', color:'#666', display:'block', marginBottom:'3px' });
  roleLbl.textContent = 'R\u00f4le';
  const roleSelect = document.createElement('select');
  Object.assign(roleSelect.style, { width:'100%', background:'#111', border:'1px solid #252525',
    borderRadius:'6px', color:'#ccc', padding:'7px 8px', fontSize:'13px' });
  Object.values(ROLES).forEach(r => {
    const opt = document.createElement('option');
    opt.value = r; opt.textContent = roleIcon(r) + ' ' + r;
    if (r === agent.role) opt.selected = true;
    roleSelect.appendChild(opt);
  });
  roleSelect.onchange = () => { agent.role = roleSelect.value; };
  roleWrap.appendChild(roleLbl); roleWrap.appendChild(roleSelect);
  form.appendChild(roleWrap);

  form.appendChild(field('Description', 'text', agent.description || '', v => { agent.description = v; }));

  const backends = listBackends();
  if (backends.length > 0) {
    const bkWrap = el('div', {});
    const bkLbl = el('label', { fontSize:'11px', color:'#666', display:'block', marginBottom:'3px' });
    bkLbl.textContent = 'Mod\u00e8le LLM';
    const bkSelect = document.createElement('select');
    Object.assign(bkSelect.style, { width:'100%', background:'#111', border:'1px solid #252525',
      borderRadius:'6px', color:'#ccc', padding:'7px 8px', fontSize:'13px' });
    backends.forEach(b => {
      const opt = document.createElement('option');
      opt.value = b.id; opt.textContent = b.label || b.id;
      if (b.id === agent.backendId) opt.selected = true;
      bkSelect.appendChild(opt);
    });
    bkSelect.onchange = () => { agent.backendId = bkSelect.value; };
    bkWrap.appendChild(bkLbl); bkWrap.appendChild(bkSelect);
    form.appendChild(bkWrap);
  }

  const spWrap = el('div', {});
  const spLbl = el('label', { fontSize:'11px', color:'#666', display:'block', marginBottom:'3px' });
  spLbl.textContent = 'System Prompt';
  const spArea = document.createElement('textarea');
  spArea.value = agent.system_prompt || '';
  spArea.rows = 6;
  Object.assign(spArea.style, { width:'100%', background:'#111', border:'1px solid #252525',
    borderRadius:'6px', color:'#ccc', padding:'8px', fontSize:'12px', resize:'vertical',
    fontFamily:'monospace', lineHeight:'1.4', boxSizing:'border-box' });
  spArea.oninput = () => { agent.system_prompt = spArea.value; };
  spWrap.appendChild(spLbl); spWrap.appendChild(spArea);
  form.appendChild(spWrap);

  const actRow = el('div', { display:'flex', gap:'8px', marginTop:'8px' });
  const saveBtn = btn(isNew ? '\u271a Cr\u00e9er l\'agent' : '\uD83D\uDCBE Sauvegarder', 'primary', async () => {
    agent.updatedAt = new Date().toISOString();
    if (isNew) { agent.createdAt = agent.updatedAt; state.agents.push(agent); }
    else { const idx = state.agents.findIndex(a => a.id === agent.id); if (idx >= 0) state.agents[idx] = agent; }
    await saveAgent(agent);
    showToast('Agent sauvegard\u00e9');
    state.editingAgent = null; state.view = 'agents'; rerender();
  });
  saveBtn.style.flex = '1';
  actRow.appendChild(saveBtn);
  form.appendChild(actRow);
  container.appendChild(form);
}

function field(label, type, value, onChange) {
  const wrap = el('div', {});
  const lbl = el('label', { fontSize:'11px', color:'#666', display:'block', marginBottom:'3px' });
  lbl.textContent = label;
  const inp = document.createElement('input');
  inp.type = type; inp.value = value;
  Object.assign(inp.style, { width:'100%', background:'#111', border:'1px solid #252525',
    borderRadius:'6px', color:'#ccc', padding:'7px 8px', fontSize:'13px', boxSizing:'border-box' });
  inp.oninput = () => onChange(inp.value);
  wrap.appendChild(lbl); wrap.appendChild(inp);
  return wrap;
}

// ═══════════════════════════════════════════════════════════════════════════════
// FABRIQUE VIEW
// ═══════════════════════════════════════════════════════════════════════════════
function renderFabriqueView(container, state, rerender) {
  const title = el('div', { fontSize:'14px', fontWeight:'600', color:'#ddd', marginBottom:'12px' });
  title.textContent = '\uD83C\uDFED Fabrique d\'agents';
  container.appendChild(title);
  const desc = el('div', { fontSize:'12px', color:'#666', marginBottom:'16px', lineHeight:'1.5' });
  desc.textContent = 'D\u00e9crivez l\'agent que vous souhaitez cr\u00e9er en langage naturel. Le Gardien construira sa configuration automatiquement.';
  container.appendChild(desc);
  const textarea = document.createElement('textarea');
  textarea.placeholder = 'Ex : "Un agent qui analyse mes relev\u00e9s bancaires et d\u00e9tecte les d\u00e9penses inhabituelles"';
  textarea.rows = 4;
  Object.assign(textarea.style, { width:'100%', background:'#111', border:'1px solid #252525',
    borderRadius:'8px', color:'#eee', padding:'10px', fontSize:'13px', resize:'none',
    fontFamily:'inherit', lineHeight:'1.4', boxSizing:'border-box', marginBottom:'10px' });
  container.appendChild(textarea);
  const generateBtn = btn('\u26a1 G\u00e9n\u00e9rer l\'agent', 'primary', async () => {
    const d = textarea.value.trim(); if (!d) return;
    generateBtn.textContent = '\u23f3 G\u00e9n\u00e9ration\u2026'; generateBtn.disabled = true;
    try {
      const gardener = state.agents.find(a => a.role === 'gardener');
      if (!gardener) { showToast('\u274c Agent Gardien introuvable', true); return; }
      const newAgent = await gardenerMerge(gardener, d, state.agents);
      state.agents.push(newAgent);
      await saveAgent(newAgent);
      showToast('\u2705 Agent "' + newAgent.name + '" cr\u00e9\u00e9');
      state.editingAgent = newAgent; state.view = 'edit'; rerender();
    } catch (e) {
      showToast('\u274c Erreur : ' + e.message, true);
    } finally {
      generateBtn.textContent = '\u26a1 G\u00e9n\u00e9rer l\'agent'; generateBtn.disabled = false;
    }
  });
  container.appendChild(generateBtn);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SETTINGS VIEW — Onglets par application
// ═══════════════════════════════════════════════════════════════════════════════
function renderSettings(container, state, rerender) {
  const TABS = [
    { id: 'nestor',    label: '\uD83E\uDDE0 Nestor'    },
    { id: 'bourse',    label: '\uD83D\uDCC8 Bourse'    },
    { id: 'meteo',     label: '\uD83C\uDF24 M\u00e9t\u00e9o'     },
    { id: 'musique',   label: '\uD83C\uDFB5 Musique'   },
    { id: 'domotique', label: '\uD83C\uDFE0 Domotique' },
    { id: 'systeme',   label: '\u2699\ufe0f Syst\u00e8me'    },
  ];

  let activeTab = state._settingsTab || 'nestor';

  // Barre d'onglets scrollable horizontalement — friendly smartphone
  const tabBar = el('div', {
    display: 'flex',
    gap: '4px',
    marginBottom: '14px',
    borderBottom: '1px solid #1a1a1a',
    paddingBottom: '8px',
    overflowX: 'auto',
    WebkitOverflowScrolling: 'touch',
    scrollbarWidth: 'none',
  });
  tabBar.style.msOverflowStyle = 'none';

  const renderTab = (tabId) => {
    activeTab = tabId;
    state._settingsTab = tabId;
    tabBody.innerHTML = '';
    renderTabBody(tabBody, tabId, state, rerender);
    tabBar.querySelectorAll('button').forEach(b => {
      b.style.background = b.dataset.tab === tabId ? '#1a3a2a' : '#111';
      b.style.color = b.dataset.tab === tabId ? '#5ef' : '#888';
      b.style.borderColor = b.dataset.tab === tabId ? '#2a5a3a' : '#252525';
    });
  };

  TABS.forEach(tab => {
    const b = btn(tab.label, '', () => renderTab(tab.id));
    b.dataset.tab = tab.id;
    b.style.cssText += ';font-size:11px;padding:5px 10px;white-space:nowrap;flex-shrink:0;';
    if (tab.id === activeTab) {
      b.style.background = '#1a3a2a'; b.style.color = '#5ef'; b.style.borderColor = '#2a5a3a';
    }
    tabBar.appendChild(b);
  });

  container.appendChild(tabBar);

  const tabBody = el('div', {});
  container.appendChild(tabBody);
  renderTabBody(tabBody, activeTab, state, rerender);
}

function renderTabBody(container, tabId, state, rerender) {
  /**
   * mkField — champ de saisie sans bouton Sauver.
   * Sauvegarde automatiquement à la perte de focus (onblur).
   * L'indicateur visuel discret (bordure verte temporaire) confirme la sauvegarde.
   */
  const mkField = (label, value, onSave, type = 'password', placeholder = 'Cl\u00e9 API\u2026') => {
    const wrap = el('div', { marginBottom: '12px' });
    const lbl = el('label', { fontSize: '11px', color: '#666', display: 'block', marginBottom: '3px' });
    lbl.textContent = label;
    const inp = document.createElement('input');
    inp.type = type;
    inp.value = value;
    inp.placeholder = placeholder;
    Object.assign(inp.style, {
      width: '100%',
      background: '#111',
      border: '1px solid #252525',
      borderRadius: '6px',
      color: '#ccc',
      padding: '9px 10px',
      fontSize: '14px',
      boxSizing: 'border-box',
      // Empêche iOS de zoomer sur le champ (font-size >= 16px côté rendu)
      WebkitAppearance: 'none',
    });
    // Sauvegarde au blur (sortie du champ)
    inp.addEventListener('blur', () => {
      const v = inp.value.trim();
      onSave(v);
      // Feedback visuel discret : bordure verte 1s
      inp.style.borderColor = '#2a6a3a';
      setTimeout(() => { inp.style.borderColor = '#252525'; }, 1000);
    });
    // Sauvegarde aussi sur Enter
    inp.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); inp.blur(); }
    });
    wrap.append(lbl, inp);
    return wrap;
  };

  const mkHint = (text) => {
    const hint = el('div', { fontSize:'10px', color:'#444', marginBottom:'10px', lineHeight:'1.4', fontStyle:'italic' });
    hint.textContent = text;
    return hint;
  };

  const mkSectionTitle = (text) => {
    const t = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase',
      letterSpacing:'0.06em', marginBottom:'8px', marginTop:'4px' });
    t.textContent = text;
    return t;
  };

  const mkSeparator = () => {
    const sep = el('div', { height:'1px', background:'#1a1a1a', margin:'10px 0' });
    return sep;
  };

  if (tabId === 'nestor') {
    const s = getNestorSettings();
    container.appendChild(mkSectionTitle('Cl\u00e9s API Nestor'));
    container.appendChild(mkField('Groq API Key',         s.apiKey,           v => setNestorSettings({ apiKey: v })));
    container.appendChild(mkField('Gemini API Key (TTS)', s.geminiApiKey,     v => setNestorSettings({ geminiApiKey: v })));
    container.appendChild(mkField('Serper API Key',       s.serperApiKey,     v => setNestorSettings({ serperApiKey: v })));
    container.appendChild(mkField('OpenRouter API Key',   s.openrouterApiKey, v => setNestorSettings({ openrouterApiKey: v })));
    container.appendChild(mkSeparator());
    container.appendChild(mkSectionTitle('LLM par d\u00e9faut'));
    container.appendChild(mkField('Mod\u00e8le Groq', s.model, v => setNestorSettings({ model: v }), 'text', 'llama3-70b-8192'));
    const ttsSection = el('div', { marginTop:'10px' });
    container.appendChild(mkSectionTitle('Synth\u00e8se vocale'));
    const silentToggle = el('div', { display:'flex', alignItems:'center', gap:'10px', marginBottom:'8px' });
    const silentLbl = el('span', { fontSize:'13px', color:'#bbb', flex:'1' });
    silentLbl.textContent = 'Mode silencieux';
    const silentBtn = btn(isSilentMode() ? '\uD83D\uDD07 Activ\u00e9' : '\uD83D\uDD0A D\u00e9sactiv\u00e9', '', () => {
      setSilentMode(!isSilentMode()); rerender();
    });
    silentToggle.append(silentLbl, silentBtn);
    ttsSection.appendChild(silentToggle);
    container.appendChild(ttsSection);
  }

  else if (tabId === 'bourse') {
    const s = getBourseSettings();
    container.appendChild(mkSectionTitle('Cl\u00e9s API Bourse'));
    container.appendChild(mkField('TwelveData API Key', s.twelveDataApiKey, v => setBourseSettings({ twelveDataApiKey: v })));
  }

  else if (tabId === 'meteo') {
    const s = getMeteoSettings();
    container.appendChild(mkSectionTitle('Cl\u00e9s API M\u00e9t\u00e9o'));
    container.appendChild(mkField('M\u00e9t\u00e9o-Concept API Key', s.meteoConcept, v => setMeteoSettings({ meteoConcept: v })));
  }

  else if (tabId === 'musique') {
    const s = getMusiqueSettings();
    container.appendChild(mkSectionTitle('Spotify'));
    container.appendChild(mkField('Client ID',     s.spotifyClientId,     v => setMusiqueSettings({ spotifyClientId: v }),     'password', 'Spotify Client ID'));
    container.appendChild(mkField('Client Secret', s.spotifyClientSecret, v => setMusiqueSettings({ spotifyClientSecret: v }), 'password', 'Spotify Client Secret'));
    container.appendChild(mkField('Redirect URI',  s.spotifyRedirectUri,  v => setMusiqueSettings({ spotifyRedirectUri: v }),  'text',     'https://\u2026'));
  }

  // ─── DOMOTIQUE (sans bouton Sync BLE — présent uniquement dans Système) ───
  else if (tabId === 'domotique') {
    const s = getDomotiqueSettings();

    // ── Tuya ──
    container.appendChild(mkSectionTitle('\uD83D\uDD0C Tuya Cloud'));
    container.appendChild(mkHint('Cr\u00e9er un projet sur iot.tuya.com \u2192 Cloud \u2192 Projects. Copier Access ID et Access Secret depuis l\'onglet Overview.'));
    container.appendChild(mkField('Access ID (Client ID)',           s.tuyaClientId,     v => setDomotiqueSettings({ tuyaClientId: v }),     'password', 'Client ID\u2026'));
    container.appendChild(mkField('Access Secret (Client Secret)',   s.tuyaClientSecret, v => setDomotiqueSettings({ tuyaClientSecret: v }), 'password', 'Client Secret\u2026'));

    // Région Tuya — select (onchange = sauvegarde immédiate, pas de blur)
    const regionWrap = el('div', { marginBottom:'12px' });
    const regionLbl = el('label', { fontSize:'11px', color:'#666', display:'block', marginBottom:'3px' });
    regionLbl.textContent = 'R\u00e9gion du datacenter';
    const regionSelect = document.createElement('select');
    Object.assign(regionSelect.style, { width:'100%', background:'#111', border:'1px solid #252525',
      borderRadius:'6px', color:'#ccc', padding:'7px 8px', fontSize:'13px' });
    [['eu','Europe (eu)'],['us','Am\u00e9rique (us)'],['cn','Chine (cn)'],['in','Inde (in)']].forEach(([val, label]) => {
      const opt = document.createElement('option');
      opt.value = val; opt.textContent = label;
      if (val === (s.tuyaRegion || 'eu')) opt.selected = true;
      regionSelect.appendChild(opt);
    });
    regionSelect.onchange = () => { setDomotiqueSettings({ tuyaRegion: regionSelect.value }); showToast('R\u00e9gion Tuya sauvegard\u00e9e'); };
    regionWrap.append(regionLbl, regionSelect);
    container.appendChild(regionWrap);

    container.appendChild(mkField('User ID (facultatif)', s.tuyaUserId, v => setDomotiqueSettings({ tuyaUserId: v }), 'text', 'UID Tuya li\u00e9\u2026'));

    container.appendChild(mkSeparator());

    // ── Ecovacs ──
    container.appendChild(mkSectionTitle('\uD83E\uDD16 Ecovacs DEEBOT (X8 Pro Omni)'));
    container.appendChild(mkHint('Identifiants du compte Ecovacs (app mobile). Le mot de passe sera hash\u00e9 en MD5 c\u00f4t\u00e9 agent avant envoi. Device ID auto-d\u00e9tect\u00e9 \u00e0 la premi\u00e8re synchro.'));
    container.appendChild(mkField('Email du compte',    s.ecovacsEmail,       v => setDomotiqueSettings({ ecovacsEmail: v }),       'text',     'email@\u2026'));
    container.appendChild(mkField('Mot de passe',       s.ecovacsPassword,    v => setDomotiqueSettings({ ecovacsPassword: v }),    'password', 'Mot de passe\u2026'));
    container.appendChild(mkField('Code pays',          s.ecovacsCountryCode, v => setDomotiqueSettings({ ecovacsCountryCode: v }), 'text',     'fr'));
    container.appendChild(mkField('Device ID (auto)',   s.ecovacsDeviceId,    v => setDomotiqueSettings({ ecovacsDeviceId: v }),    'text',     'Auto-d\u00e9tect\u00e9\u2026'));
  }

  // ─── SYSTÈME : seul endroit pour la synchro BLE ──────────────────────────
  else if (tabId === 'systeme') {
    container.appendChild(mkSectionTitle('Syst\u00e8me'));
    const syncAllRow = el('div', { display:'flex', gap:'8px', alignItems:'center', marginBottom:'10px' });
    const syncAllBtn = btn('\uD83D\uDCE1 Sync toutes les cl\u00e9s \u2192 ESP32', 'primary', async () => {
      syncAllBtn.textContent = '\u23f3 Sync\u2026'; syncAllBtn.disabled = true;
      try {
        const report = await syncApiKeys();
        if (report.error) {
          showToast('\u274c ' + report.error, true);
        } else {
          showToast('\u2705 Pouss\u00e9: ' + report.pushed.length + '  OK: ' + report.ok.length + '  Manquant: ' + report.missing.length);
        }
      } catch(e) {
        showToast('\u274c ' + e.message, true);
      } finally {
        syncAllBtn.textContent = '\uD83D\uDCE1 Sync toutes les cl\u00e9s \u2192 ESP32'; syncAllBtn.disabled = false;
      }
    });
    syncAllRow.appendChild(syncAllBtn);
    container.appendChild(syncAllRow);
  }
}

// ─── helpers ──────────────────────────────────────────────────────────────────
function el(tag, styles = {}) {
  const e = document.createElement(tag);
  Object.assign(e.style, styles);
  return e;
}

function btn(label, variant, onClick) {
  const b = document.createElement('button');
  b.textContent = label;
  const isPrimary = variant === 'primary';
  Object.assign(b.style, {
    padding: '7px 12px', borderRadius: '7px', fontSize: '12px', fontWeight: '500',
    cursor: 'pointer', border: '1px solid',
    background: isPrimary ? '#1a4a2a' : '#111',
    color: isPrimary ? '#5ef' : '#888',
    borderColor: isPrimary ? '#2a6a3a' : '#252525',
    transition: 'background 0.15s',
    WebkitTapHighlightColor: 'transparent',
    // Touch targets >= 44px de hauteur (responsive mobile)
    minHeight: '44px',
  });
  b.onclick = onClick;
  return b;
}

function showToast(msg, isError = false) {
  const t = document.createElement('div');
  Object.assign(t.style, {
    position:'fixed', bottom:'20px', left:'50%', transform:'translateX(-50%)',
    background: isError ? '#3a1a1a' : '#1a3a1a',
    color: isError ? '#f88' : '#5ef',
    padding:'10px 18px', borderRadius:'10px', fontSize:'13px',
    zIndex:'9999', pointerEvents:'none',
    border: '1px solid ' + (isError ? '#5a2a2a' : '#2a5a2a'),
    maxWidth: 'calc(100vw - 32px)',
    textAlign: 'center',
    boxShadow: '0 4px 16px rgba(0,0,0,0.4)',
  });
  t.textContent = msg;
  document.body.appendChild(t);
  setTimeout(() => t.remove(), 2800);
}
