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
import { renderMusiqueView as renderMusiqueContent } from './musique-view.js';

const ROLE_ICONS = {
  orchestrator: '🧠', gardener: '🌿', factory: '🏭',
  'monthly-payments': '📅', 'pea-portfolio': '📈', stories: '📚',
  research: '🔍', 'web-search': '🌐', 'web-analyst': '🔎', generic: '🤖',
  maison: '🏠',
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

  if (state.view === 'hub')    { renderHubView(container, state, rerender); return; }
  if (state.view === 'chat' && state.activeAgent) { renderChatView(container, state, rerender); return; }
  if (state.view === 'edit' && state.editingAgent) { renderEditView(container, state, rerender); return; }
  if (state.view === 'fabrique') { renderFabriqueView(container, state, rerender); return; }
  if (state.view === 'radar')  { renderRadarSection(container, state, rerender); return; }
  if (state.view === 'bourse') { renderBourseSection(container, state, rerender); return; }
  if (state.view === 'meteo')  { renderMeteoView(container, state, rerender); return; }
  if (state.view === 'musique'){ renderMusiqueView(container, state, rerender); return; }
  if (state.view === 'companion'){ renderCompanionView(container, state, rerender); return; }

  // Vues agents et réglages — bouton retour hub + actions agents
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

// ─── Radar section ────────────────────────────────────────────────────────────
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

  renderRadarView(body, state, rerender, () => {
    state.view = state._radarPrevView || 'hub';
    rerender();
  });
}

// ─── Bourse section ─────────────────────────────────────────────────────────
function renderBourseSection(container, state, rerender) {
  const header = el('div', { display:'flex', alignItems:'center', gap:'8px', marginBottom:'12px' });
  const title = el('div', { fontWeight:'600', fontSize:'15px', flex:'1' });
  title.textContent = '📈 Bourse & Marchés';
  header.appendChild(title);

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

// ─── Météo section ────────────────────────────────────────────────────────────
function renderMeteoSection(container, state, rerender) {
  const backBtn = btn('← Hub', '', () => { state.view = 'hub'; rerender(); });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);

  const body = el('div', {});
  container.appendChild(body);
  renderMeteoContent(body, state, rerender);
}

// ─── Musique section ──────────────────────────────────────────────────────────
function renderMusiqueSection(container, state, rerender) {
  const backBtn = btn('← Hub', '', () => { state.view = 'hub'; rerender(); });
  backBtn.style.marginBottom = '10px';
  container.appendChild(backBtn);

  const body = el('div', {});
  container.appendChild(body);
  renderMusiqueContent(body, state, rerender);
}
