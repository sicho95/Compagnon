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
         getMeteoSettings, getMusiqueSettings, setNestorSettings, setBourseSettings,
         setMeteoSettings, setMusiqueSettings } from '../core/settings-store.js';
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
  const backBtn = btn('← Hub', '', () => { state.view = 'hub'; rerender(); });
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
  const backBtn = btn('← Hub', '', () => {
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
  const backBtn = btn('← Hub', '', () => {
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
  const backBtn = btn('← Hub', '', () => { state.view = state._meteoPrevView || 'hub'; rerender(); });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);
  const body = el('div', {});
  container.appendChild(body);
  renderMeteoContent(body, state, rerender);
}

// ─── Musique ──────────────────────────────────────────────────────────────────
function renderMusiqueSection(container, state, rerender) {
  const backBtn = btn('← Hub', '', () => {
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
// Correction : l'export de companion.js est renderCompanionView (pas renderCompanionPanel)
function renderCompanionSection(container, state, rerender) {
  const backBtn = btn('← Hub', '', () => { state.view = 'hub'; rerender(); });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);
  const body = el('div', {});
  container.appendChild(body);
  import('./companion.js').then(({ renderCompanionView }) => {
    renderCompanionView(body, state, rerender);
  }).catch(() => {
    body.textContent = '⚠️ Module Companion ESP32 non disponible.';
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
  bleStatus.textContent = deviceStatus.connected ? '● ESP32 connecté' : '○ ESP32 déconnecté';
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
    { icon:'\uD83C\uDF24', label:'Météo', view:'meteo', action: () => { state._meteoPrevView = 'hub'; state.view = 'meteo'; rerender(); }},
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
    sectionLabel.textContent = 'Agents récents';
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
      const arr = el('span', { color:'#444', fontSize:'12px' }); arr.textContent = '›';
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
    empty.textContent = 'Démarrez la conversation…';
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
    thinking.textContent = '…';
    messages.appendChild(thinking);
  }
  wrap.appendChild(messages);

  const inputRow = el('div', {
    display:'flex', gap:'6px', alignItems:'flex-end',
    borderTop:'1px solid #1a1a1a', paddingTop:'8px',
  });

  const textarea = document.createElement('textarea');
  textarea.placeholder = 'Message…';
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
    sttBtn.setAttribute('aria-label', 'Dictée vocale');
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
      sttBtn.textContent = '⏹';
    };
    inputRow.appendChild(sttBtn);
  }

  const sendBtn = document.createElement('button');
  sendBtn.textContent = '→';
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
      const reply = await orchestratorResolve(agent, state.chatHistory, state.agents);
      state.chatHistory.push({ role:'assistant', content: reply });
      saveChatHistory(agent.id, state.chatHistory.filter(m => m.role !== 'system'));
      const ttsStatus = getTTSStatus();
      if (ttsStatus.enabled && !isSilentMode()) speak(textForTTS(reply));
    } catch (e) {
      state.chatHistory.push({ role:'assistant', content: '⚠️ Erreur : ' + e.message });
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
    .replace(/^[-•]\s+(.+)$/gm, '<div style="padding-left:12px">• $1</div>')
    .replace(/\n/g, '<br>');
}

// ═══════════════════════════════════════════════════════════════════════════════
// AGENTS LIST
// ═══════════════════════════════════════════════════════════════════════════════
function renderAgentsList(container, state, rerender) {
  const agents = state.agents || [];
  if (agents.length === 0) {
    const empty = el('div', { textAlign:'center', color:'#555', padding:'30px 0' });
    empty.textContent = 'Aucun agent configuré.';
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
    const editBtn = btn('✏️', '', (e) => {
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
  const exportBtn = btn('⬇ Export JSON', '', () => {
    downloadText(exportAgentsJson(state.agents), 'nestor-agents.json', 'application/json');
  });
  exportBtn.style.flex = '1';
  const importBtn = btn('⬆ Import JSON', '', async () => {
    const input = document.createElement('input');
    input.type = 'file'; input.accept = '.json';
    input.onchange = async (e) => {
      const file = e.target.files[0]; if (!file) return;
      const text = await file.text();
      const result = await importAgentsJson(text, state.agents);
      state.agents = result;
      showToast('Agents importés : ' + result.length);
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
  roleLbl.textContent = 'Rôle';
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
    bkLbl.textContent = 'Modèle LLM';
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
  const saveBtn = btn(isNew ? '✚ Créer l\'agent' : '💾 Sauvegarder', 'primary', async () => {
    agent.updatedAt = new Date().toISOString();
    if (isNew) { agent.createdAt = agent.updatedAt; state.agents.push(agent); }
    else { const idx = state.agents.findIndex(a => a.id === agent.id); if (idx >= 0) state.agents[idx] = agent; }
    await saveAgent(agent);
    showToast('Agent sauvegardé');
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
  desc.textContent = 'Décrivez l\'agent que vous souhaitez créer en langage naturel. Le Gardien construira sa configuration automatiquement.';
  container.appendChild(desc);
  const textarea = document.createElement('textarea');
  textarea.placeholder = 'Ex : "Un agent qui analyse mes relevés bancaires et détecte les dépenses inhabituelles"';
  textarea.rows = 4;
  Object.assign(textarea.style, { width:'100%', background:'#111', border:'1px solid #252525',
    borderRadius:'8px', color:'#eee', padding:'10px', fontSize:'13px', resize:'none',
    fontFamily:'inherit', lineHeight:'1.4', boxSizing:'border-box', marginBottom:'10px' });
  container.appendChild(textarea);
  const generateBtn = btn('⚡ Générer l\'agent', 'primary', async () => {
    const d = textarea.value.trim(); if (!d) return;
    generateBtn.textContent = '⏳ Génération…'; generateBtn.disabled = true;
    try {
      const gardener = state.agents.find(a => a.role === 'gardener');
      if (!gardener) { showToast('❌ Agent Gardien introuvable', true); return; }
      const newAgent = await gardenerMerge(gardener, d, state.agents);
      state.agents.push(newAgent);
      await saveAgent(newAgent);
      showToast('✅ Agent "' + newAgent.name + '" créé');
      state.editingAgent = newAgent; state.view = 'edit'; rerender();
    } catch (e) {
      showToast('❌ Erreur : ' + e.message, true);
    } finally {
      generateBtn.textContent = '⚡ Générer l\'agent'; generateBtn.disabled = false;
    }
  });
  container.appendChild(generateBtn);
}

// ═══════════════════════════════════════════════════════════════════════════════
// SETTINGS VIEW — Onglets par application
// ═══════════════════════════════════════════════════════════════════════════════
function renderSettings(container, state, rerender) {
  // ── Onglets ─────────────────────────────────────────────────────────────────
  const TABS = [
    { id: 'nestor',  label: '\uD83E\uDDE0 Nestor'  },
    { id: 'bourse',  label: '\uD83D\uDCC8 Bourse'  },
    { id: 'meteo',   label: '\uD83C\uDF24 Météo'   },
    { id: 'musique', label: '\uD83C\uDFB5 Musique' },
    { id: 'systeme', label: '⚙️ Système'  },
  ];

  let activeTab = state._settingsTab || 'nestor';

  const tabBar = el('div', { display:'flex', gap:'4px', marginBottom:'14px', borderBottom:'1px solid #1a1a1a', paddingBottom:'8px', flexWrap:'wrap' });

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
    b.style.cssText += ';font-size:11px;padding:5px 10px;';
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
  const mkField = (label, value, onSave, type = 'password', placeholder = 'Clé API…') => {
    const wrap = el('div', { marginBottom:'12px' });
    const lbl = el('label', { fontSize:'11px', color:'#666', display:'block', marginBottom:'3px' });
    lbl.textContent = label;
    const row = el('div', { display:'flex', gap:'6px', alignItems:'center' });
    const inp = document.createElement('input');
    inp.type = type; inp.value = value; inp.placeholder = placeholder;
    Object.assign(inp.style, { flex:'1', background:'#111', border:'1px solid #252525',
      borderRadius:'6px', color:'#ccc', padding:'7px 8px', fontSize:'13px', boxSizing:'border-box' });
    const saveBtn = btn('Sauver', 'primary', () => { onSave(inp.value.trim()); showToast(label + ' sauvegardé'); });
    saveBtn.style.cssText += ';padding:6px 10px;font-size:12px;white-space:nowrap;';
    row.append(inp, saveBtn);
    wrap.append(lbl, row);
    return wrap;
  };

  if (tabId === 'nestor') {
    const s = getNestorSettings();
    const sectionTitle = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'10px' });
    sectionTitle.textContent = 'Clés API Nestor';
    container.appendChild(sectionTitle);
    container.appendChild(mkField('Groq API Key',        s.apiKey,          v => setNestorSettings({ apiKey: v })));
    container.appendChild(mkField('Gemini API Key (TTS)', s.geminiApiKey,    v => setNestorSettings({ geminiApiKey: v })));
    container.appendChild(mkField('Serper API Key',       s.serperApiKey,    v => setNestorSettings({ serperApiKey: v })));
    container.appendChild(mkField('OpenRouter API Key',   s.openrouterApiKey, v => setNestorSettings({ openrouterApiKey: v })));

    const sep = el('div', { height:'1px', background:'#1a1a1a', margin:'10px 0' });
    container.appendChild(sep);

    const modelTitle = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'8px' });
    modelTitle.textContent = 'LLM par défaut';
    container.appendChild(modelTitle);
    container.appendChild(mkField('Modèle Groq', s.model, v => setNestorSettings({ model: v }), 'text', 'llama3-70b-8192'));

    const ttsSection = el('div', { marginTop:'10px' });
    const ttsTitle = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'8px' });
    ttsTitle.textContent = 'Synthèse vocale';
    ttsSection.appendChild(ttsTitle);
    const silentToggle = el('div', { display:'flex', alignItems:'center', gap:'10px', marginBottom:'8px' });
    const silentLbl = el('span', { fontSize:'13px', color:'#bbb', flex:'1' });
    silentLbl.textContent = 'Mode silencieux';
    const silentBtn = btn(isSilentMode() ? '\uD83D\uDD07 Activé' : '\uD83D\uDD0A Désactivé', '', () => {
      setSilentMode(!isSilentMode()); rerender();
    });
    silentToggle.append(silentLbl, silentBtn);
    ttsSection.appendChild(silentToggle);
    container.appendChild(ttsSection);
  }

  else if (tabId === 'bourse') {
    const s = getBourseSettings();
    const title = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'10px' });
    title.textContent = 'Clés API Bourse';
    container.appendChild(title);
    container.appendChild(mkField('TwelveData API Key', s.twelveDataApiKey, v => setBourseSettings({ twelveDataApiKey: v })));
  }

  else if (tabId === 'meteo') {
    const s = getMeteoSettings();
    const title = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'10px' });
    title.textContent = 'Clés API Météo';
    container.appendChild(title);
    container.appendChild(mkField('Météo-Concept API Key', s.meteoConcept, v => setMeteoSettings({ meteoConcept: v })));
    container.appendChild(mkField('Latitude par défaut', String(s.defaultLat), v => setMeteoSettings({ defaultLat: parseFloat(v) || 48.8566 }), 'text', '48.8566'));
    container.appendChild(mkField('Longitude par défaut', String(s.defaultLon), v => setMeteoSettings({ defaultLon: parseFloat(v) || 2.3522 }), 'text', '2.3522'));
  }

  else if (tabId === 'musique') {
    const s = getMusiqueSettings();
    const title = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'10px' });
    title.textContent = 'Spotify Connect';
    container.appendChild(title);
    container.appendChild(mkField('Client ID Spotify',     s.spotifyClientId,     v => setMusiqueSettings({ spotifyClientId: v })));
    container.appendChild(mkField('Client Secret Spotify', s.spotifyClientSecret, v => setMusiqueSettings({ spotifyClientSecret: v })));
    container.appendChild(mkField('Redirect URI',          s.spotifyRedirectUri,  v => setMusiqueSettings({ spotifyRedirectUri: v }), 'text', 'https://…'));
  }

  else if (tabId === 'systeme') {
    // ── Sync BLE clés ────────────────────────────────────────────────────────
    const bleTitle = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'8px' });
    bleTitle.textContent = 'Synchronisation clés → ESP32';
    container.appendChild(bleTitle);

    const statusEl = el('div', { fontSize:'11px', color:'#555', marginBottom:'8px', lineHeight:'1.6' });
    statusEl.textContent = bleConnected() ? '\uD83D\uDD17 ESP32 connecté' : '⚠️ ESP32 non connecté — connexion BLE requise';
    container.appendChild(statusEl);

    const syncBtn = btn('\uD83D\uDD04 Synchroniser les clés vers l\'ESP32', bleConnected() ? 'primary' : '', async () => {
      if (!bleConnected()) { showToast('⚠️ Connecte d\'abord l\'ESP32 via Bluetooth', true); return; }
      syncBtn.textContent = '⏳ Sync…'; syncBtn.disabled = true;
      try {
        const report = await syncApiKeys();
        const parts = [];
        if (report.pushed.length) parts.push('✅ ' + report.pushed.length + ' poussée(s)');
        if (report.ok.length)     parts.push('\uD83D\uDFE2 ' + report.ok.length + ' déjà OK');
        if (report.missing.length) parts.push('⚠️ ' + report.missing.length + ' manquante(s) dans les deux');
        showToast(parts.join(' · ') || 'Rien à faire');
      } catch (e) {
        showToast('❌ ' + e.message, true);
      } finally {
        syncBtn.textContent = '\uD83D\uDD04 Synchroniser les clés vers l\'ESP32';
        syncBtn.disabled = false;
      }
    });
    container.appendChild(syncBtn);

    const sep = el('div', { height:'1px', background:'#1a1a1a', margin:'16px 0' });
    container.appendChild(sep);

    // ── Danger zone ──────────────────────────────────────────────────────────
    const dangerTitle = el('div', { fontSize:'12px', color:'#555', fontWeight:'600', textTransform:'uppercase', letterSpacing:'0.06em', marginBottom:'8px' });
    dangerTitle.textContent = 'Danger';
    container.appendChild(dangerTitle);

    const resetBtn = btn('\uD83D\uDDD1 Réinitialiser tous les agents', '', async () => {
      if (!confirm('Réinitialiser tous les agents ? Cette action est irréversible.')) return;
      const { DEFAULT_AGENTS } = await import('../core/default-agents.js');
      for (const a of DEFAULT_AGENTS) { await saveAgent(a); }
      state.agents = [...DEFAULT_AGENTS];
      showToast('Agents réinitialisés');
      state.view = 'hub'; rerender();
    });
    resetBtn.style.cssText += ';color:#e65;border-color:#4a1a1a;';
    container.appendChild(resetBtn);

    const noteEl = el('div', { fontSize:'11px', color:'#444', marginTop:'12px', lineHeight:'1.5' });
    noteEl.innerHTML = '\uD83D\uDD11 Les clés API sont stockées dans localStorage sur cet appareil uniquement.<br>Elles sont poussées automatiquement vers l\'ESP32 à chaque connexion BLE.';
    container.appendChild(noteEl);
  }
}

// ═══════════════════════════════════════════════════════════════════════════════
// HELPERS UI
// ═══════════════════════════════════════════════════════════════════════════════
function el(tag, styles) {
  const e = document.createElement(tag);
  if (styles) Object.assign(e.style, styles);
  return e;
}

function btn(label, variant, onClick) {
  const b = document.createElement('button');
  b.textContent = label;
  const base = 'border-radius:6px;padding:7px 12px;font-size:12px;cursor:pointer;font-weight:500;transition:background 0.15s;-webkit-tap-highlight-color:transparent;';
  if (variant === 'primary') {
    b.style.cssText = base + 'background:#1a4a2a;border:1px solid #2a6a3a;color:#5ef;';
  } else {
    b.style.cssText = base + 'background:#111;border:1px solid #252525;color:#888;';
  }
  if (onClick) b.onclick = onClick;
  return b;
}

let toastTimer = null;
function showToast(message, isError = false) {
  let toast = document.getElementById('nestor-toast');
  if (!toast) {
    toast = document.createElement('div');
    toast.id = 'nestor-toast';
    Object.assign(toast.style, {
      position:'fixed', bottom:'80px', left:'50%', transform:'translateX(-50%)',
      padding:'8px 16px', borderRadius:'8px', fontSize:'12px', fontWeight:'500',
      zIndex:'9999', transition:'opacity 0.3s', pointerEvents:'none', whiteSpace:'nowrap',
    });
    document.body.appendChild(toast);
  }
  toast.textContent = message;
  toast.style.background = isError ? '#3a1a1a' : '#1a3a2a';
  toast.style.color = isError ? '#f88' : '#8ef';
  toast.style.border = '1px solid ' + (isError ? '#6a2a2a' : '#2a5a3a');
  toast.style.opacity = '1';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toast.style.opacity = '0'; }, 3000);
}
