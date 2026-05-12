// bourse-view.js — Vue Bourse intégrée dans Nestor v2
// Cours en temps réel via Twelve Data API (proxy CORS)
// Symboles configurables : CAC40, BTC/EUR, EUR/USD, XAU/USD (or)

import { lsGet, lsSet } from '../storage/agents-db.js';
import { getBourseSettings } from '../core/settings-store.js';

const LS_LAST_DATA = 'NESTOR_BOURSE_CACHE';
const LS_CUSTOM    = 'NESTOR_BOURSE_CUSTOM';

const DEFAULT_SYMBOLS = [
  { symbol: 'CAC40',   label: 'CAC 40',    flag: '🇫🇷' },
  { symbol: 'BTC/EUR', label: 'Bitcoin',   flag: '₿'   },
  { symbol: 'EUR/USD', label: 'EUR/USD',   flag: '💱'  },
  { symbol: 'XAU/USD', label: 'Or (once)', flag: '🥇'  },
];

// Heures de marché (9h-18h heure locale, lundi-vendredi)
function isMarketOpen() {
  const now = new Date();
  const day = now.getDay(); // 0=dim, 6=sam
  if (day === 0 || day === 6) return false;
  const h = now.getHours();
  return h >= 9 && h < 18;
}

function proxyUrl(url) {
  const proxy = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  return proxy + '?url=' + encodeURIComponent(url);
}

// ─── Fetch via Twelve Data ────────────────────────────────────────────────────
async function fetchQuotes(symbols, apiKey) {
  const joined = symbols.join(',');
  const url = `https://api.twelvedata.com/quote?symbol=${encodeURIComponent(joined)}&apikey=${apiKey}&dp=2`;
  const res = await fetch(proxyUrl(url), { signal: AbortSignal.timeout(10000) });
  if (!res.ok) throw new Error('HTTP ' + res.status);
  const data = await res.json();

  // L'API retourne soit un objet (1 symbole) soit un map {SYMBOL: obj}
  const result = {};
  if (symbols.length === 1) {
    result[symbols[0]] = data;
  } else {
    for (const sym of symbols) result[sym] = data[sym] || null;
  }
  return result;
}

// ─── DOM helper ───────────────────────────────────────────────────────────────
function el(tag, styles) {
  const e = document.createElement(tag);
  if (styles) Object.assign(e.style, styles);
  return e;
}
function btn(text, variant, onClick) {
  const b = document.createElement('button');
  b.textContent = text;
  Object.assign(b.style, {
    padding: variant === 'primary' ? '8px 16px' : '6px 12px',
    background: variant === 'primary' ? '#1a3a1a' : '#222',
    color: variant === 'primary' ? '#7ef' : '#ccc',
    border: '1px solid ' + (variant === 'primary' ? '#2a5a2a' : '#333'),
    borderRadius: '8px', fontSize: '13px', cursor: 'pointer',
  });
  b.onclick = onClick;
  return b;
}

// ─── Render ───────────────────────────────────────────────────────────────────
export function renderBourseView(container, _state, _rerender) {
  container.innerHTML = '';

  // Lecture depuis settings-store (avec fallback sur ancienne clé localStorage)
  const settings = getBourseSettings();
  const apiKey = (
    settings.twelveDataApiKey ||
    lsGet('TWELVE_DATA_KEY') ||
    ''
  ).trim();

  const wrap = el('div', { display: 'flex', flexDirection: 'column', gap: '10px' });

  // ── Pas de clé → message de config ────────────────────────────────────────
  if (!apiKey) {
    const noKey = el('div', {
      background: '#1a1010', border: '1px solid #3a2020', borderRadius: '12px',
      padding: '16px', textAlign: 'center', color: '#c88',
    });
    noKey.innerHTML = `
      <div style="font-size:28px;margin-bottom:8px">📈</div>
      <div style="font-weight:600;margin-bottom:6px">Clé Twelve Data requise</div>
      <div style="font-size:12px;color:#888;line-height:1.5">
        Crée un compte gratuit sur
        <a href="https://twelvedata.com" target="_blank" style="color:#5af">twelvedata.com</a>
        et saisis ta clé dans les <b>Réglages → onglet Bourse</b>.
      </div>`;
    wrap.appendChild(noKey);
    container.appendChild(wrap);
    return;
  }

  // ── Fusionner les symboles par défaut avec les instruments personnalisés ──
  const customs = (() => { try { return JSON.parse(lsGet(LS_CUSTOM) || '[]'); } catch { return []; } })();
  const symbols = [
    ...DEFAULT_SYMBOLS,
    ...customs.map(c => ({ symbol: c.symbol, label: c.label || c.symbol, flag: '📊' })),
  ];
  const cards = {};
  const grid = el('div', { display: 'flex', flexDirection: 'column', gap: '8px' });

  for (const s of symbols) {
    const card = el('div', {
      background: '#0e0e16', border: '1px solid #1a1a2a', borderRadius: '12px',
      padding: '12px 14px', display: 'flex', alignItems: 'center', gap: '12px',
    });

    const flagEl = el('div', { fontSize: '24px', flexShrink: '0', width: '32px', textAlign: 'center' });
    flagEl.textContent = s.flag;

    const labelCol = el('div', { flex: '1', minWidth: '0' });
    const labelEl = el('div', { fontSize: '13px', fontWeight: '600', color: '#ccc' });
    labelEl.textContent = s.label;
    const symEl = el('div', { fontSize: '10px', color: '#555' });
    symEl.textContent = s.symbol;
    labelCol.append(labelEl, symEl);

    const priceCol = el('div', { textAlign: 'right', flexShrink: '0' });
    const priceEl = el('div', { fontSize: '20px', fontWeight: '700', color: '#fff' });
    priceEl.textContent = '…';
    const changeEl = el('div', { fontSize: '11px', marginTop: '2px' });
    changeEl.textContent = '';
    priceCol.append(priceEl, changeEl);

    card.append(flagEl, labelCol, priceCol);
    grid.appendChild(card);
    cards[s.symbol] = { priceEl, changeEl, card };
  }

  // ── Status / refresh ───────────────────────────────────────────────────────
  const footer = el('div', { display: 'flex', alignItems: 'center', gap: '8px', marginTop: '4px' });
  const statusEl = el('div', { fontSize: '11px', color: '#555', flex: '1' });
  statusEl.textContent = '⏳ Chargement…';
  const refreshBtn = btn('↻', '', () => loadData(true));
  refreshBtn.title = 'Actualiser';
  footer.append(statusEl, refreshBtn);

  wrap.append(grid, footer);
  container.appendChild(wrap);

  // ── Marché fermé banner ────────────────────────────────────────────────────
  const closedBanner = el('div', {
    background: '#1a1a0a', border: '1px solid #2a2a0a', borderRadius: '8px',
    padding: '8px 12px', fontSize: '11px', color: '#aa8', display: 'none', textAlign: 'center',
  });
  closedBanner.textContent = '🕐 Marché fermé — données de la dernière séance';
  container.insertBefore(closedBanner, wrap);

  // ── Load data ──────────────────────────────────────────────────────────────
  let refreshTimer = null;

  async function loadData(force = false) {
    if (!force && !isMarketOpen()) {
      // Utiliser le cache si disponible
      const cached = JSON.parse(lsGet(LS_LAST_DATA) || 'null');
      if (cached) { renderData(cached); statusEl.textContent = 'Marché fermé — cache'; return; }
    }

    closedBanner.style.display = isMarketOpen() ? 'none' : 'block';
    refreshBtn.disabled = true;
    statusEl.textContent = '⏳ Actualisation…';

    try {
      const symList = symbols.map(s => s.symbol);
      const data = await fetchQuotes(symList, apiKey);
      lsSet(LS_LAST_DATA, JSON.stringify(data));
      renderData(data);
      const now = new Date().toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
      statusEl.textContent = `Mis à jour ${now}` + (isMarketOpen() ? '' : ' · marché fermé');
    } catch (e) {
      statusEl.textContent = '⚠️ Erreur : ' + e.message;
      // Essayer le cache
      const cached = JSON.parse(lsGet(LS_LAST_DATA) || 'null');
      if (cached) renderData(cached);
    }
    refreshBtn.disabled = false;

    // Planifier prochain refresh (90s si marché ouvert)
    if (refreshTimer) clearTimeout(refreshTimer);
    if (isMarketOpen()) refreshTimer = setTimeout(() => loadData(), 90_000);
  }

  function renderData(data) {
    for (const s of symbols) {
      const q = data[s.symbol];
      const { priceEl, changeEl, card } = cards[s.symbol];

      if (!q || q.status === 'error' || !q.close) {
        priceEl.textContent = 'N/A';
        changeEl.textContent = q?.message || '—';
        changeEl.style.color = '#555';
        continue;
      }

      const price = parseFloat(q.close);
      const prev  = parseFloat(q.previous_close);
      const chg   = parseFloat(q.change || (price - prev));
      const pct   = parseFloat(q.percent_change || ((chg / prev) * 100));
      const up    = chg >= 0;

      // Formatage du prix selon le symbole
      let displayPrice;
      if (s.symbol === 'EUR/USD') {
        displayPrice = price.toFixed(4);
      } else if (price > 1000) {
        displayPrice = price.toLocaleString('fr-FR', { maximumFractionDigits: 0 });
      } else {
        displayPrice = price.toFixed(2);
      }

      priceEl.textContent = displayPrice;
      priceEl.style.color = up ? '#7ef' : '#f77';

      const arrow = up ? '▲' : '▼';
      const sign  = up ? '+' : '';
      changeEl.textContent = `${arrow} ${sign}${chg.toFixed(2)} (${sign}${pct.toFixed(2)}%)`;
      changeEl.style.color = up ? '#4a8a4a' : '#8a3a3a';

      card.style.borderColor = up ? '#1a3a1a' : '#2a1a1a';
    }
  }

  loadData();

  // Nettoyage quand la vue est démontée (appelé par dashboard si on change de vue)
  container._cleanupBourse = () => { if (refreshTimer) clearTimeout(refreshTimer); refreshTimer = null; };
}

export function cleanupBourseView(container) {
  container?._cleanupBourse?.();
}
