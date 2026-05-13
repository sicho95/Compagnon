/**
 * companion.js — Vue gestion du Compagnon ESP32 dans Nestor PWA
 * Connexion BLE (Android/Desktop via Web Bluetooth)
 * iOS : guide d'appairage natif Bluetooth + deep link réglages
 */
import { bleAvailable, bleConnect, bleDisconnect, bleConnected, bleDeviceName } from '../bt/ble.js';
import { deviceStatus, subscribeBleStatus, setBleConnected, setBleDisconnected } from '../bt/ble_status.js';
import { scanWifiNetworks, provisionWifi, getSavedNetworks } from '../device/provisioning.js';
import { syncAgentsWithDevice } from '../sync/agents_sync.js';
import { getDeviceConfig, saveDeviceConfig, pushDeviceConfig, pushBatteryConfig, pullBatteryStatus, voltageToSoc, BATTERY_PROFILE } from '../device/device_settings.js';
import { setupLlmRelay } from '../bt/ble_protocol.js';
import { createKeyboardOverlay } from '../input/bt_keyboard.js';
import { callLLM, listBackends } from '../api/backends.js';

// Détecte iOS/iPadOS (Web Bluetooth non supporté par Apple)
function isIOS() {
  return /iP(hone|ad|od)/i.test(navigator.userAgent)
    || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
}

export function renderCompanionView(container, state, rerender) {
  container.innerHTML = '';
  container.style.cssText = 'display:flex;flex-direction:column;height:100%;overflow-y:auto;';

  if (isIOS() || !bleAvailable()) {
    renderIOSBluetoothGuide(container);
    return;
  }

  renderWebBluetoothUI(container, state, rerender);
}

// ─────────────────────────────────────────────────────────────────────────────
// MODAL MOT DE PASSE WIFI (remplace window.prompt() bloqué en PWA standalone)
// ─────────────────────────────────────────────────────────────────────────────
/**
 * Affiche une modal HTML pour saisir le mot de passe WiFi.
 * Retourne une Promise<string|null> :
 *   - string  → mot de passe saisi (peut être vide pour réseaux ouverts)
 *   - null    → l'utilisateur a annulé
 */
function askWifiPassword(ssid) {
  return new Promise((resolve) => {
    // Overlay sombre
    const overlay = el('div',
      'position:fixed;inset:0;background:rgba(0,0,0,0.75);z-index:9999;' +
      'display:flex;align-items:center;justify-content:center;padding:16px;');

    const card = el('div',
      'background:#141414;border:1px solid #2a2a2a;border-radius:14px;' +
      'padding:20px 16px;width:100%;max-width:340px;display:flex;flex-direction:column;gap:14px;');

    // Titre
    const title = el('div', 'font-size:14px;font-weight:700;color:#eee;');
    title.textContent = '🔒 Mot de passe WiFi';

    // SSID affiché
    const ssidLbl = el('div', 'font-size:12px;color:#7af;');
    ssidLbl.textContent = ssid;

    // Input mot de passe
    const inp = document.createElement('input');
    inp.type = 'password';
    inp.placeholder = 'Mot de passe…';
    inp.autocomplete = 'current-password';
    inp.style.cssText =
      'width:100%;background:#1a1a1a;border:1px solid #333;border-radius:8px;' +
      'color:#eee;padding:11px 12px;font-size:16px;box-sizing:border-box;outline:none;' +
      '-webkit-appearance:none;';

    // Toggle afficher/masquer
    const toggleRow = el('div', 'display:flex;align-items:center;gap:8px;cursor:pointer;');
    const toggleCb  = document.createElement('input');
    toggleCb.type = 'checkbox';
    toggleCb.style.cssText = 'width:16px;height:16px;cursor:pointer;accent-color:#7af;';
    const toggleLbl = el('span', 'font-size:12px;color:#888;');
    toggleLbl.textContent = 'Afficher le mot de passe';
    toggleCb.onchange = () => { inp.type = toggleCb.checked ? 'text' : 'password'; };
    toggleRow.append(toggleCb, toggleLbl);

    // Boutons
    const btnRow = el('div', 'display:flex;gap:8px;');

    const cancelBtn = document.createElement('button');
    cancelBtn.textContent = 'Annuler';
    cancelBtn.style.cssText =
      'flex:1;padding:11px;border:1px solid #333;border-radius:8px;' +
      'background:#1a1a1a;color:#888;font-size:13px;font-weight:600;cursor:pointer;';

    const connectBtn = document.createElement('button');
    connectBtn.textContent = '→ Connecter';
    connectBtn.style.cssText =
      'flex:2;padding:11px;border:1px solid #2a6a3a;border-radius:8px;' +
      'background:#1a3a1a;color:#7ef;font-size:13px;font-weight:600;cursor:pointer;';

    const close = (val) => { overlay.remove(); resolve(val); };

    cancelBtn.onclick  = () => close(null);
    connectBtn.onclick = () => close(inp.value);

    // Confirmer avec Entrée
    inp.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') { e.preventDefault(); close(inp.value); }
      if (e.key === 'Escape') close(null);
    });

    // Clic hors carte = annuler
    overlay.onclick = (e) => { if (e.target === overlay) close(null); };

    btnRow.append(cancelBtn, connectBtn);
    card.append(title, ssidLbl, inp, toggleRow, btnRow);
    overlay.appendChild(card);
    document.body.appendChild(overlay);

    // Focus auto avec délai (évite le blocage iOS)
    requestAnimationFrame(() => { setTimeout(() => inp.focus(), 80); });
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// GUIDE iOS : appairage Bluetooth natif
// ─────────────────────────────────────────────────────────────────────────────
function renderIOSBluetoothGuide(container) {
  const wrap = el('div', 'padding:16px;display:flex;flex-direction:column;gap:12px;');

  const header = el('div', 'text-align:center;padding:12px 0 4px;');
  header.innerHTML = `
    <div style="font-size:40px;margin-bottom:8px;">📱</div>
    <div style="font-size:15px;font-weight:700;color:#eee;">Compagnon ESP32</div>
    <div style="font-size:12px;color:#555;margin-top:4px;">iOS — Bluetooth natif requis</div>
  `;
  wrap.appendChild(header);

  const infoBanner = el('div', 'background:#1a1208;border:1px solid #3a2a08;border-radius:10px;padding:12px 14px;');
  infoBanner.innerHTML = `
    <div style="font-size:12px;color:#fa8;font-weight:600;margin-bottom:6px;">⚠️ Web Bluetooth non disponible sur iOS Safari</div>
    <div style="font-size:11px;color:#766040;line-height:1.6;">
      Apple ne supporte pas Web Bluetooth sur iOS/iPadOS.<br>
      L'appairage se fait via les <strong style="color:#fa8;">Réglages Bluetooth</strong> natifs,
      puis la PWA communique via les fonctionnalités disponibles (WiFi, sync, etc.).
    </div>
  `;
  wrap.appendChild(infoBanner);

  const stepsTitle = el('div', 'font-size:11px;color:#555;text-transform:uppercase;letter-spacing:0.06em;margin-top:4px;');
  stepsTitle.textContent = 'Comment appairer le Compagnon';
  wrap.appendChild(stepsTitle);

  const steps = [
    { n:'1', icon:'📲', title:'Allume le Compagnon ESP32', desc:'Appuie sur le bouton power. L\'écran affiche « Prêt à appairer » et le nom BLE : <strong style="color:#7ef;">Nestor</strong>.' },
    { n:'2', icon:'⚙️', title:'Ouvre les Réglages Bluetooth', desc:'Va dans <strong style="color:#7ef;">Réglages → Bluetooth</strong> sur ton iPhone et assure-toi que le Bluetooth est activé.' },
    { n:'3', icon:'🔍', title:'Cherche « Nestor » dans la liste', desc:'L\'ESP32 apparaît dans la section <em>Autres appareils</em>. Appuie dessus pour appairer.' },
    { n:'4', icon:'✅', title:'Confirme le code d\'appairage', desc:'Si un code PIN s\'affiche sur l\'écran du Compagnon, saisis-le sur l\'iPhone. Accepte la demande d\'appairage.' },
    { n:'5', icon:'🌐', title:'Reviens dans la PWA', desc:'Une fois appairé, le Compagnon se connecte automatiquement en WiFi si configuré. La sync d\'agents et les clés API se font via WiFi.' },
  ];

  for (const s of steps) {
    const row = el('div', 'display:flex;gap:12px;align-items:flex-start;background:#0e0e0e;border:1px solid #1a1a1a;border-radius:10px;padding:12px;');
    const num = el('div', 'min-width:28px;height:28px;border-radius:50%;background:#1a3a1a;border:1px solid #2a5a2a;display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:700;color:#7ef;flex-shrink:0;');
    num.textContent = s.n;
    const txt = el('div', 'flex:1;');
    txt.innerHTML = `<div style="font-size:13px;font-weight:600;color:#ddd;margin-bottom:3px;">${s.icon} ${s.title}</div><div style="font-size:11px;color:#666;line-height:1.6;">${s.desc}</div>`;
    row.append(num, txt);
    wrap.appendChild(row);
  }

  const btBtn = document.createElement('button');
  btBtn.style.cssText = 'background:#1a2a3a;border:1px solid #2a4a6a;border-radius:10px;padding:13px 16px;color:#7af;font-size:13px;font-weight:600;cursor:pointer;display:flex;align-items:center;justify-content:center;gap:8px;width:100%;margin-top:4px;';
  btBtn.innerHTML = '<span style="font-size:18px;">⚙️</span> Ouvrir Réglages Bluetooth iOS';
  btBtn.onclick = () => {
    window.location.href = 'App-Prefs:root=Bluetooth';
    setTimeout(() => { window.location.href = 'prefs:root=Bluetooth'; }, 400);
  };
  wrap.appendChild(btBtn);

  const noteAndroid = el('div', 'background:#0a1a0a;border:1px solid #1a2a1a;border-radius:8px;padding:10px 12px;margin-top:4px;');
  noteAndroid.innerHTML = `
    <div style="font-size:11px;color:#3a6a3a;font-weight:600;margin-bottom:3px;">✅ Sur Android</div>
    <div style="font-size:11px;color:#2a4a2a;line-height:1.5;">
      Web Bluetooth est supporté nativement sur Chrome Android.<br>
      Utilise l'onglet <strong>Connecter</strong> ci-dessous depuis un appareil Android pour le flux complet.
    </div>
  `;
  wrap.appendChild(noteAndroid);

  const noteSync = el('div', 'background:#0a0a1a;border:1px solid #1a1a2a;border-radius:8px;padding:10px 12px;');
  noteSync.innerHTML = `
    <div style="font-size:11px;color:#3a3a7a;font-weight:600;margin-bottom:3px;">💡 Sync agents & clés sur iOS</div>
    <div style="font-size:11px;color:#2a2a4a;line-height:1.5;">
      Une fois le Compagnon connecté au WiFi (via un Android ou depuis l'ESP32 directement),
      la synchronisation des agents et des clés API se fait <strong>via WiFi</strong> — pas besoin de BLE.
    </div>
  `;
  wrap.appendChild(noteSync);

  container.appendChild(wrap);
}

// ─────────────────────────────────────────────────────────────────────────────
// WEB BLUETOOTH UI (Android / Desktop Chrome)
// ─────────────────────────────────────────────────────────────────────────────
function renderWebBluetoothUI(container, state, rerender) {
  const connBar = el('div', 'display:flex;align-items:center;justify-content:space-between;padding:12px 16px;background:#0e1a0e;border-bottom:1px solid #1a2a1a;position:sticky;top:0;z-index:2;');
  const connInfo = el('div', 'display:flex;flex-direction:column;gap:2px;');
  const connectBtn = document.createElement('button');
  connectBtn.style.cssText = 'padding:8px 14px;border:1px solid #333;border-radius:8px;font-size:12px;font-weight:600;cursor:pointer;';

  function refreshBar() {
    const ok = bleConnected();
    const mode = deviceStatus.mode;
    const modeLabel = mode === 'wifi' ? '📶 ' + (deviceStatus.wifiSSID || 'WiFi')
      : mode === 'ble_relay' ? '📱 Relais LLM' : '⚡ BLE seulement';
    connInfo.innerHTML = ok
      ? `<span style="color:#7ef;font-size:13px;font-weight:600;">🔗 ${bleDeviceName() || 'Nestor'}</span><span style="color:#5a7a5a;font-size:11px;">${modeLabel}</span>`
      : `<span style="color:#888;font-size:13px;">Compagnon non connecté</span><span style="color:#444;font-size:11px;">Chrome Android ou Chrome Desktop requis</span>`;
    connectBtn.textContent = ok ? 'Déconnecter' : 'Connecter';
    connectBtn.style.background = ok ? '#2a1a1a' : '#1a3a1a';
    connectBtn.style.color = ok ? '#e87' : '#7ef';
  }

  connectBtn.onclick = async () => {
    if (bleConnected()) {
      await bleDisconnect(); setBleDisconnected();
    } else {
      try {
        connectBtn.textContent = '⏳…'; connectBtn.disabled = true;
        const name = await bleConnect();
        setBleConnected(name);
        const backends = listBackends();
        const defaultB = backends[0];
        setupLlmRelay(async (messages) => {
          const choice = await callLLM(defaultB?.id || 'groq-llama', { messages });
          return choice?.message?.content || '';
        });
        const cfg = getDeviceConfig();
        if (cfg.ble.autoSyncAgents) {
          try { const merged = await syncAgentsWithDevice(); state.agents = merged; rerender(); } catch {}
        }
      } catch (e) { alert('Connexion échouée: ' + e.message); }
      finally { connectBtn.disabled = false; }
    }
    refreshBar(); renderSections();
  };

  connBar.append(connInfo, connectBtn);
  container.appendChild(connBar);
  refreshBar();

  const sectionsWrap = el('div', 'display:flex;flex-direction:column;');
  container.appendChild(sectionsWrap);

  function renderSections() {
    sectionsWrap.innerHTML = '';
    if (!bleConnected()) {
      const hint = el('div', 'padding:40px 16px;text-align:center;color:#444;font-size:13px;line-height:1.7;');
      hint.innerHTML = `<div style="font-size:40px;margin-bottom:12px;">📡</div>
        <div style="color:#555;">Connecte-toi au Compagnon ESP32<br>pour gérer WiFi, agents, clavier et config.</div>
        <div style="color:#333;font-size:11px;margin-top:8px;">Chrome Android ou Chrome Desktop requis</div>`;
      sectionsWrap.appendChild(hint);
      return;
    }
    sectionsWrap.appendChild(mkSection('📶 Réseau WiFi',       buildWifiSection()));
    sectionsWrap.appendChild(mkSection('🧠 Agents & Sync',     buildAgentsSection(state, rerender)));
    sectionsWrap.appendChild(mkSection('🔋 Batterie & PMIC',   buildBatterySection()));
    sectionsWrap.appendChild(mkSection('⌨️ Clavier distant',   buildKeyboardSection()));
    sectionsWrap.appendChild(mkSection('⚙️ Configuration',     buildConfigSection()));
  }
  renderSections();

  subscribeBleStatus(() => { refreshBar(); renderSections(); });
}

// ── WiFi ────────────────────────────────────────────────────
function buildWifiSection() {
  const wrap = el('div', 'display:flex;flex-direction:column;gap:8px;');
  const status = el('div', 'font-size:12px;color:#5a7a5a;padding:4px 0;');
  status.textContent = deviceStatus.wifi === 'connected'
    ? `✅ Connecté : ${deviceStatus.wifiSSID || '?'}` : '⚠️ Pas de WiFi sur le compagnon';
  wrap.appendChild(status);

  const saved = getSavedNetworks();
  if (saved.length > 0) {
    const lbl = el('div', 'font-size:11px;color:#555;text-transform:uppercase;letter-spacing:0.06em;margin-top:4px;');
    lbl.textContent = 'Réseaux enregistrés'; wrap.appendChild(lbl);
    for (const net of saved.slice(0, 5)) {
      const row = document.createElement('button');
      row.style.cssText = 'background:#111;border:1px solid #222;border-radius:6px;padding:8px 12px;text-align:left;color:#aaa;font-size:12px;cursor:pointer;display:flex;align-items:center;justify-content:space-between;width:100%;';
      row.innerHTML = `<span>📶 ${net.ssid}</span><span style="color:#555;font-size:11px;">→ Reconnecter</span>`;
      row.onclick = async () => {
        // Utilise la modal HTML — prompt() est bloqué en PWA standalone
        const pwd = await askWifiPassword(net.ssid);
        if (pwd === null) return;
        row.textContent = '⏳ Connexion…';
        try {
          await provisionWifi(net.ssid, pwd);
          status.textContent = `✅ Connecté : ${net.ssid}`;
        } catch (e) {
          status.textContent = '❌ ' + e.message;
          row.innerHTML = `<span>📶 ${net.ssid}</span><span style="color:#555;font-size:11px;">→ Reconnecter</span>`;
        }
      };
      wrap.appendChild(row);
    }
  }

  const scanBtn = document.createElement('button');
  scanBtn.textContent = '🔍 Scanner les réseaux WiFi';
  scanBtn.style.cssText = btnStyle('#1a2a3a', '#7af');
  scanBtn.onclick = async () => {
    scanBtn.textContent = '⏳ Scan en cours sur l\'ESP32…'; scanBtn.disabled = true;
    try { const nets = await scanWifiNetworks(); showNetworkPicker(nets, wrap, status); }
    catch (e) { status.textContent = '❌ Scan échoué: ' + e.message; }
    finally { scanBtn.textContent = '🔍 Scanner les réseaux WiFi'; scanBtn.disabled = false; }
  };
  wrap.appendChild(scanBtn);
  return wrap;
}

async function showNetworkPicker(networks, wrap, statusEl) {
  wrap.querySelector('.wifi-list')?.remove();

  // Normaliser les deux formats ESP32 : compact {s,r,sec,ch} et complet {ssid,rssi,secured,channel}
  const items = (networks || [])
    .map(net => ({
      ssid:    net?.ssid    ?? net?.s ?? '',
      rssi:    Number(net?.rssi    ?? net?.r   ?? -999),
      secured: net?.secured != null ? Boolean(net.secured) : (net?.sec != null ? Boolean(net.sec) : true),
      channel: net?.channel ?? net?.ch ?? 0,
    }))
    .filter(net => net.ssid);

  if (!items.length) {
    statusEl.textContent = '⚠️ Aucun réseau trouvé (vue de l\'ESP32)';
    return;
  }

  // Label explicite : ces réseaux sont ceux vus par la radio de l'ESP32, pas du téléphone
  statusEl.textContent = `📡 ${items.length} réseau(x) vus par l'ESP32`;

  const list = el('div', 'display:flex;flex-direction:column;gap:4px;max-height:200px;overflow-y:auto;');
  list.className = 'wifi-list';

  for (const net of items) {
    const bars    = net.rssi > -60 ? '▂▄▆█' : net.rssi > -75 ? '▂▄▆_' : net.rssi > -85 ? '▂▄__' : '▂___';
    const lock    = net.secured ? '🔒' : '🔓';
    const chLabel = net.channel ? ` ch${net.channel}` : '';

    const b = document.createElement('button');
    b.style.cssText = 'background:#111;border:1px solid #222;border-radius:6px;padding:8px 12px;color:#ccc;font-size:12px;cursor:pointer;display:flex;align-items:center;justify-content:space-between;width:100%;';
    b.innerHTML = `<span>${lock} ${net.ssid}</span><span style="color:#555;font-size:10px;">${bars} ${net.rssi}dBm${chLabel}</span>`;

    b.onclick = async () => {
      // Demander le mot de passe uniquement si le réseau est sécurisé
      const pwd = net.secured ? await askWifiPassword(net.ssid) : '';
      if (pwd === null) return;  // annulé par l'utilisateur
      b.textContent = '⏳ Connexion…';
      try {
        await provisionWifi(net.ssid, pwd, true);
        statusEl.textContent = `✅ Connecté : ${net.ssid}`;
        list.remove();
      } catch (e) {
        statusEl.textContent = '❌ ' + e.message;
        b.innerHTML = `<span>${lock} ${net.ssid}</span><span style="color:#555;font-size:10px;">${bars} ${net.rssi}dBm${chLabel}</span>`;
      }
    };
    list.appendChild(b);
  }
  wrap.appendChild(list);
}

// ── Agents ────────────────────────────────────────────────
function buildAgentsSection(state, rerender) {
  const wrap = el('div', 'display:flex;flex-direction:column;gap:8px;');
  const info = el('div', 'font-size:11px;color:#555;');
  info.textContent = deviceStatus.lastSync
    ? `Dernière sync : ${new Date(deviceStatus.lastSync).toLocaleTimeString()}`
    : 'Aucune sync effectuée';
  wrap.appendChild(info);

  const syncBtn = document.createElement('button');
  syncBtn.textContent = '🔄 Synchroniser les agents';
  syncBtn.style.cssText = btnStyle('#1a3a1a', '#7ef');
  syncBtn.onclick = async () => {
    syncBtn.textContent = '⏳ Sync…'; syncBtn.disabled = true;
    try {
      const merged = await syncAgentsWithDevice();
      state.agents = merged;
      info.textContent = `✅ Sync OK — ${merged.length} agent(s) — ${new Date().toLocaleTimeString()}`;
      rerender();
    } catch (e) { info.textContent = '❌ ' + e.message; }
    finally { syncBtn.textContent = '🔄 Synchroniser les agents'; syncBtn.disabled = false; }
  };
  wrap.appendChild(syncBtn);
  wrap.appendChild(Object.assign(el('div', 'font-size:11px;color:#333;line-height:1.5;'), { textContent: `${state.agents.length} agent(s) local — fusion par date de modification` }));
  return wrap;
}

// ── Batterie & PMIC ───────────────────────────────────────
function buildBatterySection() {
  const wrap = el('div', 'display:flex;flex-direction:column;gap:8px;');
  const cfg  = getDeviceConfig();
  const bat  = cfg.battery;

  const statusRow = el('div', 'display:flex;align-items:center;gap:12px;padding:8px 0;');
  const voltEl    = el('div', 'font-size:13px;color:#7ef;font-weight:600;min-width:52px;');
  const socBar    = el('div', 'flex:1;height:8px;background:#1a1a1a;border-radius:4px;overflow:hidden;');
  const socFill   = el('div', 'height:100%;background:#3a8a5a;border-radius:4px;transition:width 0.4s;width:0%;');
  const socLabel  = el('div', 'font-size:11px;color:#555;min-width:32px;text-align:right;');
  socBar.appendChild(socFill);
  statusRow.append(voltEl, socBar, socLabel);
  wrap.appendChild(statusRow);

  async function refreshStatus() {
    try {
      const s = await pullBatteryStatus();
      const pct = s.soc;
      voltEl.textContent = (s.voltageMv / 1000).toFixed(2) + 'V' + (s.charging ? ' ⚡' : '');
      socFill.style.width = pct + '%';
      socFill.style.background = pct < 10 ? '#e87' : pct < 25 ? '#fa8' : '#3a8a5a';
      socLabel.textContent = pct + '%';
    } catch { voltEl.textContent = '—'; socLabel.textContent = '—'; }
  }
  refreshStatus();

  const params = [
    { label:'⚡ Courant charge (mA)',    key:'chargeCurrentMa',      min:100,  max:800,  step:50  },
    { label:'🔋 Tension max (mV)',        key:'chargeVoltageMv',      min:4100, max:4200, step:10  },
    { label:'🏁 Courant fin charge (mA)', key:'terminationCurrentMa', min:25,   max:100,  step:25  },
    { label:'⚠️ Alerte basse (mV)',       key:'alertLowMv',           min:3100, max:3500, step:50  },
    { label:'🛑 Coupure critique (mV)',   key:'alertCriticalMv',      min:2900, max:3200, step:50  },
  ];

  for (const p of params) {
    const row = el('div', 'display:flex;align-items:center;justify-content:space-between;gap:8px;padding:2px 0;');
    const lbl = el('div', 'font-size:11px;color:#888;flex:1;'); lbl.textContent = p.label;
    const inp = document.createElement('input');
    inp.type='number'; inp.min=p.min; inp.max=p.max; inp.step=p.step; inp.value=bat[p.key];
    inp.style.cssText='width:72px;background:#111;border:1px solid #333;border-radius:6px;padding:4px 6px;color:#7ef;font-size:12px;text-align:right;';
    inp.onchange=()=>{ bat[p.key]=Number(inp.value); saveDeviceConfig(cfg); };
    row.append(lbl,inp); wrap.appendChild(row);
  }

  wrap.appendChild(mkToggle('🔬 Jauge AXP2101 interne', bat.gaugeEnabled, v=>{ bat.gaugeEnabled=v; saveDeviceConfig(cfg); }));

  const modeRow = el('div','display:flex;align-items:center;justify-content:space-between;gap:8px;padding:2px 0;');
  const modeLbl = el('div','font-size:11px;color:#888;'); modeLbl.textContent='📊 Affichage batterie';
  const modeBtn = el('button','');
  let dispMode = bat.displayMode||'percent';
  const refreshMode=()=>{ modeBtn.textContent=dispMode==='percent'?'% capacité':'V tension'; modeBtn.style.cssText='padding:4px 10px;border:1px solid #333;border-radius:12px;font-size:11px;cursor:pointer;background:#1a1a1a;color:#aaa;'; };
  modeBtn.onclick=()=>{ dispMode=dispMode==='percent'?'voltage':'percent'; bat.displayMode=dispMode; saveDeviceConfig(cfg); refreshMode(); };
  refreshMode(); modeRow.append(modeLbl,modeBtn); wrap.appendChild(modeRow);

  const refreshBtn=document.createElement('button'); refreshBtn.textContent='🔄 Actualiser statut batterie';
  refreshBtn.style.cssText=btnStyle('#0a1a0a','#5af');
  refreshBtn.onclick=async()=>{ refreshBtn.textContent='⏳…'; await refreshStatus(); refreshBtn.textContent='🔄 Actualiser statut batterie'; };
  wrap.appendChild(refreshBtn);

  const pushBtn=document.createElement('button'); pushBtn.textContent='📤 Envoyer config PMIC au Compagnon';
  pushBtn.style.cssText=btnStyle('#2a1a0a','#fa8');
  pushBtn.onclick=async()=>{
    pushBtn.textContent='⏳ Envoi…'; pushBtn.disabled=true;
    try { await pushBatteryConfig(bat); pushBtn.textContent='✅ PMIC configuré'; }
    catch { pushBtn.textContent='❌ Erreur envoi'; }
    finally { setTimeout(()=>{ pushBtn.textContent='📤 Envoyer config PMIC au Compagnon'; pushBtn.disabled=false; },2500); }
  };
  wrap.appendChild(pushBtn);

  const info=el('div','font-size:10px;color:#333;line-height:1.6;margin-top:4px;');
  info.textContent=`${BATTERY_PROFILE.model} · ${BATTERY_PROFILE.capacityMah}mAh · ${BATTERY_PROFILE.chemistry} · ${BATTERY_PROFILE.voltageMin}–${BATTERY_PROFILE.voltageFull}V`;
  wrap.appendChild(info);
  return wrap;
}

// ── Clavier ──────────────────────────────────────────────────
function buildKeyboardSection() {
  const wrap=el('div','display:flex;flex-direction:column;gap:8px;');
  const desc=el('div','font-size:12px;color:#555;line-height:1.5;');
  desc.textContent='Utilise le clavier de ton téléphone pour écrire dans le chat du Compagnon sans parler.';
  const kbBtn=document.createElement('button');
  kbBtn.textContent='⌨️ Ouvrir le clavier distant';
  kbBtn.style.cssText=btnStyle('#2a1a3a','#c7f');
  kbBtn.onclick=()=>document.body.appendChild(createKeyboardOverlay(()=>{}));
  wrap.append(desc,kbBtn);
  return wrap;
}

// ── Config ────────────────────────────────────────────────
function buildConfigSection() {
  const wrap=el('div','display:flex;flex-direction:column;gap:8px;');
  const cfg=getDeviceConfig();
  wrap.appendChild(mkToggle('📱 Relais LLM si pas de WiFi',        cfg.ble.relayLlmOnNoWifi, v=>{ cfg.ble.relayLlmOnNoWifi=v; saveDeviceConfig(cfg); }));
  wrap.appendChild(mkToggle('🔄 Sync auto agents à la connexion',   cfg.ble.autoSyncAgents,   v=>{ cfg.ble.autoSyncAgents=v;   saveDeviceConfig(cfg); }));
  const pushBtn=document.createElement('button'); pushBtn.textContent='📤 Envoyer la config au Compagnon';
  pushBtn.style.cssText=btnStyle('#2a2a1a','#fa8');
  pushBtn.onclick=async()=>{
    pushBtn.textContent='⏳ Envoi…'; pushBtn.disabled=true;
    try { await pushDeviceConfig(cfg); pushBtn.textContent='✅ Config envoyée'; }
    catch { pushBtn.textContent='❌ Erreur'; }
    finally { setTimeout(()=>{ pushBtn.textContent='📤 Envoyer la config au Compagnon'; pushBtn.disabled=false; },2000); }
  };
  wrap.appendChild(pushBtn);
  return wrap;
}

// ── Helpers DOM ─────────────────────────────────────────────
function el(tag,css){ const e=document.createElement(tag); if(css) e.style.cssText=css; return e; }
function btnStyle(bg,color){ return `background:${bg};color:${color};border:1px solid ${color}22;border-radius:8px;padding:10px 14px;font-size:12px;font-weight:600;cursor:pointer;width:100%;text-align:left;`; }
function mkSection(title,content){
  const w=el('div','background:#0a0a0a;border-bottom:1px solid #1a1a1a;');
  const h=el('button','width:100%;background:none;border:none;text-align:left;padding:12px 16px;color:#888;font-size:12px;font-weight:600;display:flex;align-items:center;justify-content:space-between;cursor:pointer;text-transform:uppercase;letter-spacing:0.06em;');
  h.innerHTML=`<span>${title}</span><span style="font-size:10px;color:#444;">▼</span>`;
  const b=el('div','padding:0 16px 14px;'); b.appendChild(content);
  let open=true;
  h.onclick=()=>{ open=!open; b.style.display=open?'':'none'; h.querySelector('span:last-child').textContent=open?'▼':'▶'; };
  w.append(h,b); return w;
}
function mkToggle(label,value,onChange){
  const row=el('div','display:flex;align-items:center;justify-content:space-between;gap:8px;padding:4px 0;');
  const lbl=el('div','font-size:12px;color:#888;flex:1;'); lbl.textContent=label;
  const btn=el('button','');
  let state=value;
  const refresh=()=>{ btn.textContent=state?'ON':'OFF'; btn.style.cssText=`padding:4px 10px;border:none;border-radius:12px;font-size:11px;font-weight:600;cursor:pointer;${state?'background:#1a3a1a;color:#7ef;':'background:#1a1a1a;color:#555;'}`; };
  btn.onclick=()=>{ state=!state; refresh(); onChange(state); };
  refresh(); row.append(lbl,btn); return row;
}
