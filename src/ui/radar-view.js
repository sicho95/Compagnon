// radar-view.js — Vue Radars intégrée dans Nestor v2
// Porte la PWA sicho95/Radars : détection GPS + alertes radars en temps réel
// Sources: Lufop API (primaire) + Blitzer.de (secondaire), via proxy CORS

import { lsGet } from '../storage/agents-db.js';

// ─── Haversine distance (mètres) ─────────────────────────────────────────────
function haversine(lat1, lon1, lat2, lon2) {
  const R = 6371e3;
  const p1 = lat1 * Math.PI / 180;
  const p2 = lat2 * Math.PI / 180;
  const dlat = (lat2 - lat1) * Math.PI / 180;
  const dlon = (lon2 - lon1) * Math.PI / 180;
  const a = Math.sin(dlat / 2) ** 2 + Math.cos(p1) * Math.cos(p2) * Math.sin(dlon / 2) ** 2;
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
}

// ─── Bearing (degrés 0-360) ───────────────────────────────────────────────────
function bearing(lat1, lon1, lat2, lon2) {
  const dLon = (lon2 - lon1) * Math.PI / 180;
  const y = Math.sin(dLon) * Math.cos(lat2 * Math.PI / 180);
  const x = Math.cos(lat1 * Math.PI / 180) * Math.sin(lat2 * Math.PI / 180)
          - Math.sin(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) * Math.cos(dLon);
  return ((Math.atan2(y, x) * 180 / Math.PI) + 360) % 360;
}

// ─── Audio ────────────────────────────────────────────────────────────────────
let _audioCtx = null;
function getAudioCtx() {
  if (!_audioCtx || _audioCtx.state === 'closed') {
    _audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  }
  return _audioCtx;
}
function beep(freq = 1000, duration = 200) {
  try {
    const ctx = getAudioCtx();
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.frequency.value = freq;
    gain.gain.setValueAtTime(0.35, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + duration / 1000);
    osc.start();
    osc.stop(ctx.currentTime + duration / 1000);
  } catch (e) { console.warn('[Radar/audio]', e.message); }
}

// ─── Proxy ────────────────────────────────────────────────────────────────────
function proxyUrl(url) {
  const proxy = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  return proxy + '?url=' + encodeURIComponent(url);
}

// ─── Fetch radars Lufop ───────────────────────────────────────────────────────
async function fetchLufop(lat, lon, radiusKm = 20) {
  const url = `https://api.lufop.net/api?format=json&nbr=100&q=${lat},${lon}&m=${radiusKm}&pays=fr`;
  try {
    const res = await fetch(proxyUrl(url), { signal: AbortSignal.timeout(8000) });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    return (data?.radars || data?.features || []).map(r => ({
      lat: r.lat ?? r.geometry?.coordinates?.[1],
      lon: r.lng ?? r.lon ?? r.geometry?.coordinates?.[0],
      speedLimit: r.vitesse ?? r.speed ?? r.properties?.speed ?? 0,
      type: r.type ?? r.properties?.type ?? 'fixe',
    })).filter(r => r.lat && r.lon);
  } catch (e) {
    console.warn('[Radar/lufop]', e.message);
    return [];
  }
}

// ─── Fetch radars Blitzer ─────────────────────────────────────────────────────
async function fetchBlitzer(lat, lon, radiusKm = 20) {
  const deg = radiusKm / 111;
  const box = `${lat - deg},${lon - deg},${lat + deg},${lon + deg}`;
  const url = `https://cdn2.atudo.net/api/4.0/pois.php?z=9&type=0,1,2,3,4,5,ra,w&box=${encodeURIComponent(box)}`;
  try {
    const res = await fetch(proxyUrl(url), { signal: AbortSignal.timeout(8000) });
    if (!res.ok) throw new Error('HTTP ' + res.status);
    const data = await res.json();
    return (data?.pois || []).map(r => ({
      lat: parseFloat(r.lat),
      lon: parseFloat(r.lng),
      speedLimit: parseInt(r.vmax || r.speedLimit || '0', 10),
      type: r.type ?? 'fixe',
    })).filter(r => r.lat && r.lon);
  } catch (e) {
    console.warn('[Radar/blitzer]', e.message);
    return [];
  }
}

// ─── État global de la vue (survit aux re-renders) ────────────────────────────
const RS = {
  watchId: null,
  wakeLock: null,
  radars: [],
  lastFetch: 0,
  fetchLat: null,
  fetchLon: null,
  pos: null,         // { latitude, longitude, speed, heading }
  nearest: null,     // { dist, speedLimit, angle }
  alertZone: false,
  alerted: false,
  audioUnlocked: false,
  refreshHandle: null,
  _updateUI: null,   // callback injecté par renderRadarView
};

const FETCH_INTERVAL_MS = 90_000;  // re-fetch radars toutes les 90s ou si déplacé > 5km
const ALERT_DISTANCE_M = 500;      // alerte à 500m
const ALERT_AFTER_M = 150;         // fin d'alerte à 150m après le radar

function distanceMoved(lat1, lon1, lat2, lon2) {
  if (lat1 == null) return Infinity;
  return haversine(lat1, lon1, lat2, lon2);
}

async function refreshRadars(lat, lon) {
  RS.fetchLat = lat; RS.fetchLon = lon; RS.lastFetch = Date.now();
  const [lufop, blitzer] = await Promise.all([
    fetchLufop(lat, lon, 20),
    fetchBlitzer(lat, lon, 20),
  ]);
  // Déduplique par position proche (<50m)
  const merged = [...lufop];
  for (const b of blitzer) {
    if (!merged.some(r => haversine(r.lat, r.lon, b.lat, b.lon) < 50)) {
      merged.push(b);
    }
  }
  RS.radars = merged;
  RS._updateUI?.();
}

function computeNearest(pos) {
  if (!RS.radars.length || !pos) { RS.nearest = null; return; }
  let best = null, bestDist = Infinity;
  for (const r of RS.radars) {
    const d = haversine(pos.latitude, pos.longitude, r.lat, r.lon);
    if (d < bestDist) { bestDist = d; best = r; }
  }
  if (best) {
    RS.nearest = {
      dist: Math.round(bestDist),
      speedLimit: best.speedLimit || 0,
      angle: bearing(pos.latitude, pos.longitude, best.lat, best.lon),
      type: best.type,
    };
  } else {
    RS.nearest = null;
  }
}

function handleAlerts(speedKmh) {
  const d = RS.nearest?.dist ?? Infinity;
  const wasInZone = RS.alertZone;
  RS.alertZone = d <= ALERT_DISTANCE_M;

  if (RS.alertZone && !wasInZone) {
    // Entrée dans la zone → bip d'entrée
    RS.alerted = false;
    beep(1500, 300);
  }
  if (RS.alertZone && !RS.alerted && speedKmh > (RS.nearest?.speedLimit || 999)) {
    beep(2000, 400);
    RS.alerted = true;
  }
  if (!RS.alertZone) { RS.alerted = false; }
}

function startGPS() {
  if (!navigator.geolocation) return;
  if (RS.watchId != null) return;

  RS.watchId = navigator.geolocation.watchPosition(
    async (geoPos) => {
      const { latitude, longitude, speed, heading } = geoPos.coords;
      RS.pos = { latitude, longitude, speed, heading };

      // Rafraîchir radars si besoin
      const moved = distanceMoved(RS.fetchLat, RS.fetchLon, latitude, longitude);
      if (!RS.lastFetch || Date.now() - RS.lastFetch > FETCH_INTERVAL_MS || moved > 5000) {
        refreshRadars(latitude, longitude);
      }

      computeNearest(RS.pos);
      handleAlerts((speed || 0) * 3.6);
      RS._updateUI?.();
    },
    (err) => { console.warn('[Radar/GPS]', err.message); RS._updateUI?.(); },
    { enableHighAccuracy: true, maximumAge: 0, timeout: 5000 }
  );
}

function stopGPS() {
  if (RS.watchId != null) {
    navigator.geolocation.clearWatch(RS.watchId);
    RS.watchId = null;
  }
  RS.wakeLock?.release().catch(() => {});
  RS.wakeLock = null;
  if (RS.refreshHandle) { clearInterval(RS.refreshHandle); RS.refreshHandle = null; }
}

async function requestWakeLock() {
  try {
    if ('wakeLock' in navigator) RS.wakeLock = await navigator.wakeLock.request('screen');
  } catch (e) { console.warn('[Radar/wakeLock]', e.message); }
}

// ─── Unlock audio sur premier geste utilisateur ───────────────────────────────
function unlockAudio() {
  if (RS.audioUnlocked) return;
  RS.audioUnlocked = true;
  try { getAudioCtx().resume(); } catch {}
}

// ─── DOM helpers ──────────────────────────────────────────────────────────────
function el(tag, styles) {
  const e = document.createElement(tag);
  if (styles) Object.assign(e.style, styles);
  return e;
}

// ─── Render principal ─────────────────────────────────────────────────────────
export function renderRadarView(container, _state, _rerender, onStop) {
  container.innerHTML = '';
  container.addEventListener('click', unlockAudio, { once: true });

  // ── Layout ─────────────────────────────────────────────────────────────────
  const wrap = el('div', {
    display: 'flex', flexDirection: 'column', alignItems: 'center',
    gap: '16px', padding: '8px 0', color: '#fff',
  });

  // ── Speedometer ────────────────────────────────────────────────────────────
  const speedBox = el('div', {
    textAlign: 'center', background: '#0a0a1a', borderRadius: '16px',
    padding: '20px 40px', border: '2px solid #1a1a3a', width: '100%', boxSizing: 'border-box',
  });
  const speedNum = el('div', { fontSize: '64px', fontWeight: '700', lineHeight: '1', color: '#fff' });
  speedNum.textContent = '0';
  const speedUnit = el('div', { fontSize: '13px', color: '#666', marginTop: '2px' });
  speedUnit.textContent = 'km/h';
  speedBox.append(speedNum, speedUnit);

  // ── Radar panel ────────────────────────────────────────────────────────────
  const radarBox = el('div', {
    textAlign: 'center', background: '#0a1a0a', borderRadius: '16px',
    padding: '16px', border: '2px solid #1a3a1a', width: '100%', boxSizing: 'border-box',
  });
  const radarIcon = el('div', { fontSize: '36px', marginBottom: '4px' });
  radarIcon.textContent = '📡';
  const radarDist = el('div', { fontSize: '28px', fontWeight: '700', color: '#7ef' });
  radarDist.textContent = '—';
  const radarLimit = el('div', { fontSize: '14px', color: '#888', marginTop: '4px' });
  radarLimit.textContent = 'Aucun radar détecté';
  const radarGauge = el('div', { height: '6px', background: '#1a3a1a', borderRadius: '3px', marginTop: '10px', overflow: 'hidden' });
  const radarBar = el('div', { height: '100%', width: '0%', background: '#3a8a3a', borderRadius: '3px', transition: 'width 0.4s, background 0.4s' });
  radarGauge.appendChild(radarBar);
  radarBox.append(radarIcon, radarDist, radarLimit, radarGauge);

  // ── Status bar ─────────────────────────────────────────────────────────────
  const statusEl = el('div', { fontSize: '11px', color: '#555', textAlign: 'center' });
  statusEl.textContent = '⏳ Démarrage GPS…';

  // ── Stats ──────────────────────────────────────────────────────────────────
  const statsEl = el('div', { fontSize: '11px', color: '#444', textAlign: 'center' });

  // ── Bouton stop ────────────────────────────────────────────────────────────
  const stopBtn = document.createElement('button');
  Object.assign(stopBtn.style, {
    padding: '10px 24px', background: '#2a0a0a', color: '#f88',
    border: '1px solid #4a1a1a', borderRadius: '10px', fontSize: '13px', cursor: 'pointer', width: '100%',
  });
  stopBtn.textContent = '⏹ Arrêter la surveillance';
  stopBtn.onclick = () => {
    stopGPS();
    onStop?.();
  };

  wrap.append(speedBox, radarBox, statusEl, statsEl, stopBtn);
  container.appendChild(wrap);

  // ── Mise à jour UI ─────────────────────────────────────────────────────────
  RS._updateUI = () => {
    const pos = RS.pos;
    const kmh = pos ? Math.round((pos.speed || 0) * 3.6) : 0;
    speedNum.textContent = String(kmh);

    // Couleur vitesse selon limite
    const limit = RS.nearest?.speedLimit || 0;
    if (limit && kmh > limit + 5) speedNum.style.color = '#f44';
    else if (limit && kmh > limit) speedNum.style.color = '#fa0';
    else speedNum.style.color = '#fff';

    // Panneau radar
    if (RS.nearest) {
      const { dist, speedLimit, type } = RS.nearest;
      radarDist.textContent = dist >= 1000 ? (dist / 1000).toFixed(1) + ' km' : dist + ' m';
      radarLimit.textContent = speedLimit ? '⚠️ Limite : ' + speedLimit + ' km/h' : '⚠️ ' + (type || 'radar fixe');
      radarBox.style.borderColor = RS.alertZone ? '#f44' : '#1a3a1a';
      radarBox.style.background = RS.alertZone ? '#1a0a0a' : '#0a1a0a';
      radarIcon.textContent = RS.alertZone ? '🚨' : '📡';
      // Gauge : pleine à ALERT_DISTANCE_M, vide à 2x
      const ratio = Math.max(0, Math.min(1, 1 - (dist / (ALERT_DISTANCE_M * 2))));
      radarBar.style.width = (ratio * 100) + '%';
      radarBar.style.background = dist < 200 ? '#f44' : dist < ALERT_DISTANCE_M ? '#fa0' : '#3a8a3a';
    } else {
      radarDist.textContent = '—';
      radarLimit.textContent = 'Aucun radar détecté';
      radarBox.style.borderColor = '#1a3a1a';
      radarBox.style.background = '#0a1a0a';
      radarIcon.textContent = '📡';
      radarBar.style.width = '0%';
    }

    // Status
    if (!pos) {
      statusEl.textContent = '⏳ Attente signal GPS…';
    } else {
      const acc = pos.accuracy ? Math.round(pos.accuracy) + 'm' : '?';
      statusEl.textContent = `📍 GPS actif — précision ±${acc}`;
    }

    // Stats
    statsEl.textContent = RS.radars.length
      ? `${RS.radars.length} radars dans les ${20} km`
      : RS.lastFetch ? '0 radar dans la zone' : '';
  };

  // Démarrage
  requestWakeLock();
  startGPS();
  RS._updateUI();
}

// Nettoyage quand on quitte la vue
export function cleanupRadarView() {
  stopGPS();
  RS._updateUI = null;
  RS.pos = null;
  RS.nearest = null;
  RS.alertZone = false;
  RS.alerted = false;
}
