import { loadAgents, loadChatHistory } from './storage/agents-db.js';
import { initBackends } from './api/backends.js';
import { renderDashboard } from './ui/dashboard.js';
import { cleanupRadarView } from './ui/radar-view.js';
import { cleanupMusiqueView } from './ui/musique-view.js';

async function main() {
  const root = document.getElementById('app-root');
  if (!root) { console.error('[Nestor] #app-root introuvable'); return; }

  if ('serviceWorker' in navigator) {
    try { await navigator.serviceWorker.register('./service-worker.js'); }
    catch (e) { console.warn('[Nestor] SW:', e); }
  }

  // Callback OAuth Alexa : échange le code si présent dans l'URL
  const _urlParams  = new URLSearchParams(window.location.search);
  const _alexaCode  = _urlParams.get('code');
  const _alexaState = _urlParams.get('state');
  if (_alexaCode && _alexaState?.startsWith('nestor-alexa-')) {
    try {
      await alexa_exchange_code(_alexaCode);
      window.history.replaceState({}, '', window.location.pathname);
      console.info('[Nestor] Alexa OAuth : tokens reçus et stockés.');
    } catch (e) {
      console.warn('[Nestor] OAuth Alexa callback :', e.message);
    }
  }

  try { await initBackends(); }
  catch (e) { console.warn('[Nestor] initBackends:', e); }

  let agents = [];
  try { agents = await loadAgents(); }
  catch (e) { console.error('[Nestor] loadAgents:', e); }

  const orchestrator = agents.find(a => a.role === 'orchestrator') || null;

  const state = {
    view: 'hub',
    agents,
    activeAgent: orchestrator,
    editingAgent: null,
    chatHistory: orchestrator
      ? [{ role: 'system', content: orchestrator.system_prompt || '' }]
      : [],
    menuOpen: false,
    _radarPrevView: 'hub',
    _boursePrevView: 'hub',
  };

  renderFrame(root, state);
}

export function renderFrame(root, state) {
  const safeViews = ['hub', 'agents', 'settings', 'chat', 'edit', 'fabrique', 'radar', 'bourse', 'meteo', 'musique'];
  if (!safeViews.includes(state.view)) state.view = 'hub';

  // Nettoyage radar si on quitte la vue
  if (state._prevView === 'radar' && state.view !== 'radar') {
    cleanupRadarView();
  }
  // Fermer le drawer si on quitte le chat
  if (state.view !== 'chat') state.menuOpen = false;
  state._prevView = state.view;

  root.innerHTML = '';

  const frame = document.createElement('div');
  frame.className = 'lvgl-frame';

  const statusBar = document.createElement('div');
  statusBar.className = 'lvgl-status-bar';

  const titleEl = document.createElement('span');
  titleEl.style.cssText = 'font-weight:600;font-size:13px;flex:1;';
  const titles = {
    hub: '🏠 Compagnon',
    chat: state.activeAgent ? '🧠 ' + state.activeAgent.name : '🧠 Nestor',
    settings: '⚙️ Réglages',
    fabrique: '🏭 Fabrique',
    edit: '✏️ Édition',
    radar: '🚨 Radars',
    bourse: '📈 Bourse',
    agents: '🤖 Agents',
    meteo: '🌤 Météo',
    musique: '🎵 Musique',
  };
  titleEl.textContent = titles[state.view] || '🏠 Compagnon';

  statusBar.appendChild(titleEl);

  // Hamburger uniquement dans la vue Nestor/chat
  if (state.view === 'chat') {
    const hamburger = document.createElement('button');
    hamburger.innerHTML = state.menuOpen
      ? '✕'
      : '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="3" y1="6" x2="21" y2="6"/><line x1="3" y1="12" x2="21" y2="12"/><line x1="3" y1="18" x2="21" y2="18"/></svg>';
    hamburger.style.cssText = 'background:none;border:none;color:#aaa;padding:4px 8px;font-size:16px;cursor:pointer;-webkit-tap-highlight-color:transparent;';
    hamburger.onclick = () => {
      state.menuOpen = !state.menuOpen;
      renderFrame(root, state);
    };
    statusBar.appendChild(hamburger);
  }

  const rerender = () => renderFrame(root, state);

  // Icône ⚙️ Réglages dans la barre du hub (bug #5)
  if (state.view === 'hub') {
    const settingsBtn = document.createElement('button');
    settingsBtn.textContent = '⚙️';
    settingsBtn.title = 'Réglages';
    settingsBtn.style.cssText = 'background:none;border:none;color:#888;padding:4px 8px;font-size:16px;cursor:pointer;-webkit-tap-highlight-color:transparent;';
    settingsBtn.onclick = () => { state.view = 'settings'; rerender(); };
    statusBar.appendChild(settingsBtn);
  }

  // Hamburger uniquement dans la vue chat Nestor
  if (state.view === 'chat') {
    const hamburger = document.createElement('button');
    hamburger.innerHTML = state.menuOpen
      ? '✕'
      : '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="3" y1="6" x2="21" y2="6"/><line x1="3" y1="12" x2="21" y2="12"/><line x1="3" y1="18" x2="21" y2="18"/></svg>';
    hamburger.style.cssText = 'background:none;border:none;color:#aaa;padding:4px 8px;font-size:16px;cursor:pointer;-webkit-tap-highlight-color:transparent;';
    hamburger.onclick = () => {
      state.menuOpen = !state.menuOpen;
      renderFrame(root, state);
    };
    statusBar.appendChild(hamburger);
  }

  let drawer = null;
  let drawerOverlay = null;
  if (state.view === 'chat' && state.menuOpen) {
    drawerOverlay = document.createElement('div');
    drawerOverlay.style.cssText = 'position:absolute;inset:0;background:rgba(0,0,0,0.55);z-index:10;';
    drawerOverlay.onclick = () => { state.menuOpen = false; rerender(); };

    drawer = document.createElement('div');
    drawer.style.cssText = [
      'position:absolute;top:0;left:0;bottom:0;width:220px;',
      'background:#0e0e0e;border-right:1px solid #222;',
      'z-index:11;display:flex;flex-direction:column;padding:12px 0;',
      'overflow-y:auto;-webkit-overflow-scrolling:touch;',
      'animation:slideIn 0.18s ease;'
    ].join('');

    const drawerTitle = document.createElement('div');
    drawerTitle.style.cssText = 'padding:8px 16px 12px;font-size:11px;color:#555;text-transform:uppercase;letter-spacing:0.08em;border-bottom:1px solid #1a1a1a;margin-bottom:8px;';
    drawerTitle.textContent = 'Conversations';
    drawer.appendChild(drawerTitle);

    const mkItem = (icon, label, action, isActive) => {
      const b = document.createElement('button');
      b.style.cssText = [
        'background:' + (isActive ? '#1a2a1a' : 'none') + ';',
        'border:none;color:' + (isActive ? '#7ef' : '#bbb') + ';',
        'padding:12px 16px;text-align:left;font-size:13px;',
        'cursor:pointer;display:flex;align-items:center;gap:10px;',
        'border-left:2px solid ' + (isActive ? '#3a8a5a' : 'transparent') + ';',
        '-webkit-tap-highlight-color:transparent;width:100%;',
      ].join('');
      b.innerHTML = '<span style="font-size:16px">' + icon + '</span><span>' + label + '</span>';
      b.onclick = action;
      return b;
    };

    // ── Retour Hub ─────────────────────────────────────────────────────────
    drawer.appendChild(mkItem('🏠', 'Accueil', () => {
      state.view = 'hub'; state.menuOpen = false; rerender();
    }, false));

    const sep0 = document.createElement('div');
    sep0.style.cssText = 'height:1px;background:#1a1a1a;margin:6px 0;';
    drawer.appendChild(sep0);

    // ── Orchestrateur ──────────────────────────────────────────────────────
    const orch = state.agents.find(a => a.role === 'orchestrator');
    if (orch) {
      drawer.appendChild(mkItem('🧠', 'Parler à l\'Orchestrateur',
        () => {
          state.activeAgent = orch;
          const hist = loadChatHistory(orch.id);
          state.chatHistory = [{ role: 'system', content: orch.system_prompt || '' }, ...hist];
          state.view = 'chat';
          state.menuOpen = false;
          rerender();
        },
        state.view === 'chat' && state.activeAgent?.role === 'orchestrator'
      ));
    }

    const sep1 = document.createElement('div');
    sep1.style.cssText = 'height:1px;background:#1a1a1a;margin:6px 0;';
    drawer.appendChild(sep1);

    // ── Agents métier ──────────────────────────────────────────────────────
    const otherAgents = state.agents.filter(a =>
      a.role !== 'orchestrator' && a.role !== 'gardener' && a.role !== 'factory'
    );
    if (otherAgents.length > 0) {
      const agentsLabel = document.createElement('div');
      agentsLabel.style.cssText = 'padding:4px 16px 6px;font-size:10px;color:#444;text-transform:uppercase;letter-spacing:0.08em;';
      agentsLabel.textContent = 'Agents';
      drawer.appendChild(agentsLabel);

      const ICONS = {
        'monthly-payments': '📅', 'pea-portfolio': '📈', stories: '📚',
        research: '🔍', 'web-search': '🌐', 'web-analyst': '🔎', generic: '🤖',
        maison: '🏠',
      };
      otherAgents.forEach(a => {
        drawer.appendChild(mkItem(
          ICONS[a.role] || '🤖', a.name,
          () => {
            state.activeAgent = a;
            state.chatHistory = [{ role: 'system', content: a.system_prompt || '' }];
            state.view = 'chat';
            state.menuOpen = false;
            rerender();
          },
          state.view === 'chat' && state.activeAgent?.id === a.id
        ));
      });
    }

    const sep2 = document.createElement('div');
    sep2.style.cssText = 'height:1px;background:#1a1a1a;margin:6px 0;';
    drawer.appendChild(sep2);

    // ── Apps v2 ────────────────────────────────────────────────────────────
    const appsLabel = document.createElement('div');
    appsLabel.style.cssText = 'padding:4px 16px 6px;font-size:10px;color:#444;text-transform:uppercase;letter-spacing:0.08em;';
    appsLabel.textContent = 'Applications';
    drawer.appendChild(appsLabel);

    drawer.appendChild(mkItem('🚨', 'Radars', () => {
      state._radarPrevView = state.view;
      state.view = 'radar';
      state.menuOpen = false;
      rerender();
    }, state.view === 'radar'));

    drawer.appendChild(mkItem('📈', 'Bourse & Marchés', () => {
      state._boursePrevView = state.view;
      state.view = 'bourse';
      state.menuOpen = false;
      rerender();
    }, state.view === 'bourse'));

    const sep3 = document.createElement('div');
    sep3.style.cssText = 'height:1px;background:#1a1a1a;margin:6px 0;';
    drawer.appendChild(sep3);

    // ── Navigation ─────────────────────────────────────────────────────────
    // Note : pas de lien Réglages ici — accès via l'icône ⚙️ du hub uniquement
    drawer.appendChild(mkItem('🤖', 'Gérer les agents', () => { state.view = 'agents'; state.menuOpen = false; rerender(); }, state.view === 'agents'));
    drawer.appendChild(mkItem('🏭', 'Fabrique', () => { state.view = 'fabrique'; state.menuOpen = false; rerender(); }, state.view === 'fabrique'));
  }

  const mainEl = document.createElement('div');
  mainEl.className = 'lvgl-main';
  mainEl.style.position = 'relative';

  const content = document.createElement('div');
  content.className = 'lvgl-content';
  renderDashboard(content, state, rerender);

  mainEl.appendChild(content);
  if (drawerOverlay) mainEl.appendChild(drawerOverlay);
  if (drawer) mainEl.appendChild(drawer);

  frame.append(statusBar, mainEl);
  root.appendChild(frame);

  if (!document.getElementById('nestor-anim')) {
    const style = document.createElement('style');
    style.id = 'nestor-anim';
    style.textContent = '@keyframes slideIn{from{transform:translateX(-100%)}to{transform:translateX(0)}}';
    document.head.appendChild(style);
  }
}

main().catch(e => console.error('[Nestor] crash:', e));
