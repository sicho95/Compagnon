// meteo-view.js — Vue Météo intégrée dans Nestor v2
// Prévisions quotidiennes via api.meteo-concept.com
// Position : GPS BLE (state.gpsLat/Lon) → Paris par défaut

import { lsGet, lsSet } from '../storage/agents-db.js';
import { getMeteoSettings, getNestorSettings } from '../core/settings-store.js';

const LS_CACHE = 'NESTOR_METEO_CACHE';
const DEFAULT_LAT = 48.8566, DEFAULT_LON = 2.3522;  // Paris

// ─── Icônes météo (codes meteo-concept) ──────────────────────────────────────
function weatherIcon(code) {
  if (code === 0)                    return '☀️';
  if (code >= 1  && code <= 2)       return '🌤';
  if (code >= 3  && code <= 4)       return '⛅';
  if (code >= 5  && code <= 7)       return '🌥';
  if (code >= 8  && code <= 10)      return '☁️';
  if (code >= 11 && code <= 16)      return '🌧';
  if (code >= 40 && code <= 48)      return '⛈';
  if (code >= 60 && code <= 68)      return '❄️';
  if (code >= 70 && code <= 78)      return '🌨';
  if (code >= 100 && code <= 102)    return '🌩';
  return '🌡';
}

// ─── Proxy via settings-store (cohérent avec le reste de l'app) ──────────────
function proxyUrl(url) {
  const proxy = (getNestorSettings().proxyUrl || 'https://proxy.sicho95.workers.dev').replace(/\/$/, '');
  return proxy + '?url=' + encodeURIComponent(url);
}

// ─── Fetch direct : /forecast/daily?token=...&latlng=lat,lon ─────────────────
// Même stratégie que meteo_app.cpp sur l'ESP32 — pas d'étape /location/near
async function fetchMeteo(lat, lon, token) {
  const url = `https://api.meteo-concept.com/api/forecast/daily?token=${token}&latlng=${lat},${lon}`;
  const res = await fetch(proxyUrl(url), { signal: AbortSignal.timeout(10000) });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  const data = await res.json();

  const city = data?.city;
  const cityName = city?.name || city?.nom || `${lat.toFixed(2)}, ${lon.toFixed(2)}`;
  const forecasts = (data?.forecast || []).slice(0, 5);

  if (forecasts.length === 0) throw new Error('Aucune prévision reçue');
  return { cityName, forecasts };
}

// ─── DOM helper ───────────────────────────────────────────────────────────────
function el(tag, styles) {
  const e = document.createElement(tag);
  if (styles) Object.assign(e.style, styles);
  return e;
}

function dayLabel(dateStr, idx) {
  if (idx === 0) return 'Auj.';
  if (idx === 1) return 'Dem.';
  const d = new Date(dateStr);
  return d.toLocaleDateString('fr-FR', { weekday: 'short' });
}

// ─── Rendu d'une carte de prévision ──────────────────────────────────────────
function renderCard(fc, idx) {
  const card = el('div', {
    background: '#0e1a1e', border: '1px solid #1a3a4a', borderRadius: '14px',
    padding: '14px 10px', display: 'flex', flexDirection: 'column',
    alignItems: 'center', gap: '6px', flex: '1', minWidth: '72px',
  });

  const day = el('div', { fontSize: '11px', color: '#5a8aa0', fontWeight: '600', textTransform: 'uppercase' });
  day.textContent = dayLabel(fc.datetime, idx);

  const ico = el('div', { fontSize: '28px', lineHeight: '1' });
  ico.textContent = weatherIcon(fc.weather);

  const temps = el('div', { display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '2px' });
  const tmax = el('div', { fontSize: '15px', fontWeight: '700', color: '#FFB74D' });
  tmax.textContent = Math.round(fc.tmax) + '°';
  const tmin = el('div', { fontSize: '12px', color: '#78909C' });
  tmin.textContent = Math.round(fc.tmin) + '°';
  temps.append(tmax, tmin);

  if (fc.probarain > 0) {
    const rain = el('div', { fontSize: '11px', color: '#64B5F6' });
    rain.textContent = '💧 ' + fc.probarain + '%';
    card.append(day, ico, temps, rain);
  } else {
    card.append(day, ico, temps);
  }
  return card;
}

// ─── Rendu principal ──────────────────────────────────────────────────────────
export function renderMeteoView(container, state, rerender) {
  container.innerHTML = '';

  const settings = getMeteoSettings();
  const token = (
    settings.meteoConcept ||
    lsGet('METEO_CONCEPT_KEY') ||
    ''
  ).trim();

  const configuredLat = Number(settings.defaultLat);
  const configuredLon = Number(settings.defaultLon);
  const defaultLat = Number.isFinite(configuredLat) && configuredLat !== 0 ? configuredLat : DEFAULT_LAT;
  const defaultLon = Number.isFinite(configuredLon) && configuredLon !== 0 ? configuredLon : DEFAULT_LON;

  // En-tête localisation
  const locRow = el('div', { display: 'flex', alignItems: 'center', gap: '8px', marginBottom: '14px' });
  const locIcon = el('span', { fontSize: '16px' }); locIcon.textContent = '📍';
  const locLabel = el('div', { fontSize: '13px', color: '#7AB', flex: '1' });

  let lat, lon;
  if (state.gpsLat && state.gpsLon) {
    lat = state.gpsLat; lon = state.gpsLon;
    locLabel.textContent = `GPS BLE: ${lat.toFixed(4)}, ${lon.toFixed(4)}`;
  } else {
    lat = defaultLat; lon = defaultLon;
    locLabel.textContent = 'Paris (par défaut)';
  }
  locRow.append(locIcon, locLabel);
  container.appendChild(locRow);

  if (!token) {
    const warn = el('div', { padding: '16px', background: '#1a1000', border: '1px solid #3a2800',
      borderRadius: '10px', fontSize: '12px', color: '#FFB74D', lineHeight: '1.6' });
    warn.innerHTML = '⚠️ Clé API manquante.<br>Configure <b>METEO_CONCEPT_KEY</b> dans les Réglages → onglet Météo.<br>'
      + '<a href="https://api.meteo-concept.com" target="_blank" style="color:#5af">api.meteo-concept.com</a> (gratuit).';
    container.appendChild(warn);
    return;
  }

  const cityEl = el('div', { fontSize: '18px', fontWeight: '700', color: '#CFE8FF', marginBottom: '4px', textAlign: 'center' });
  cityEl.textContent = '⏳ Chargement…';
  container.appendChild(cityEl);

  const statusEl = el('div', { fontSize: '11px', color: '#5a7a88', textAlign: 'center', marginBottom: '16px' });
  statusEl.textContent = '';
  container.appendChild(statusEl);

  const cardsRow = el('div', { display: 'flex', gap: '8px', flexWrap: 'nowrap', overflowX: 'auto', paddingBottom: '4px' });
  container.appendChild(cardsRow);

  // Cache immédiat le temps du fetch
  const cached = (() => { try { return JSON.parse(lsGet(LS_CACHE) || 'null'); } catch { return null; } })();
  if (cached) {
    cityEl.textContent = cached.cityName;
    statusEl.textContent = 'Mis à jour ' + cached.timestamp;
    cached.forecasts.forEach((fc, i) => cardsRow.appendChild(renderCard(fc, i)));
  }

  fetchMeteo(lat, lon, token)
    .then(({ cityName, forecasts }) => {
      const timestamp = new Date().toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
      try { lsSet(LS_CACHE, JSON.stringify({ cityName, forecasts, timestamp })); } catch {}
      cityEl.textContent = cityName;
      statusEl.textContent = 'Mis à jour ' + timestamp;
      cardsRow.innerHTML = '';
      forecasts.forEach((fc, i) => cardsRow.appendChild(renderCard(fc, i)));
    })
    .catch(err => {
      statusEl.textContent = '⚠️ ' + err.message;
      statusEl.style.color = '#EF9A9A';
    });
}

export function cleanupMeteoView() {
  // Pas d'intervalles actifs à nettoyer
}
