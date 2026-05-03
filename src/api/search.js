import { lsGet, lsSet } from '../storage/agents-db.js';

// ─────────────────────────────────────────────────────────────────────────────
// search.js — Moteur de recherche web pour Nestor
// Stratégie : Serper.dev (primaire) → DuckDuckGo Instant Answer (fallback)
//
// Serper.dev :
//   - 2 500 req/mois GRATUITES, sans CB
//   - Vrais résultats Google structurés en JSON
//   - Clé SERPER_KEY à renseigner dans Réglages
//   - Si quota épuisé (HTTP 429) → bascule auto sur DuckDuckGo
//
// DuckDuckGo Instant Answer (fallback) :
//   - API publique SANS clé, sans inscription, gratuite
//   - Retourne définitions, infobox, résultats liés
//   - Limites : pas un vrai SERP complet — requêtes complexes doivent
//     être DÉCOUPÉES en sous-requêtes simples avant appel
//
// Découpage multi-requêtes (requêtes complexes) :
//   - L'appelant peut passer un tableau de queries via searchWebMulti()
//   - Chaque sous-requête est lancée indépendamment, résultats agrégés
//   - Le LLM se charge ensuite de la synthèse/reformatage
//
// Proxy CORS perso (proxy.sicho95.workers.dev) :
//   - Toutes les requêtes passent par le proxy pour contourner le CORS
// ─────────────────────────────────────────────────────────────────────────────

const DEFAULT_PROXY = 'https://proxy.sicho95.workers.dev/';
const LS_SERPER_EXHAUSTED = 'SERPER_QUOTA_EXHAUSTED_MONTH';

// ─── Helpers ─────────────────────────────────────────────────────────────────

function getProxyUrl() {
  return (lsGet('SEARCH_PROXY_URL') || DEFAULT_PROXY).replace(/\/$/, '');
}

function isSerperExhausted() {
  const stored = lsGet(LS_SERPER_EXHAUSTED);
  if (!stored) return false;
  const { month, year } = JSON.parse(stored);
  const now = new Date();
  return now.getFullYear() === year && now.getMonth() === month;
}

function markSerperExhausted() {
  const now = new Date();
  lsSet(LS_SERPER_EXHAUSTED, JSON.stringify({ month: now.getMonth(), year: now.getFullYear() }));
}

// ─── Serper.dev ──────────────────────────────────────────────────────────────

async function searchViaSerper(query, maxResults) {
  const apiKey = lsGet('SERPER_KEY') || '';
  if (!apiKey) throw new Error('SERPER_KEY manquante — configure-la dans Réglages.');

  const proxy    = getProxyUrl();
  const endpoint = 'https://google.serper.dev/search';
  const proxyUrl = proxy + '?url=' + encodeURIComponent(endpoint);

  const res = await fetch(proxyUrl, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'X-API-KEY': apiKey },
    body: JSON.stringify({ q: query, num: maxResults, gl: 'fr', hl: 'fr' }),
  });

  if (res.status === 429) {
    markSerperExhausted();
    throw new Error('SERPER_QUOTA_EXCEEDED');
  }
  if (!res.ok) {
    const txt = await res.text().catch(() => '');
    throw new Error('Serper erreur ' + res.status + (txt ? ' : ' + txt.slice(0, 120) : ''));
  }

  const data  = await res.json();
  const items = data.organic || [];
  return items.slice(0, maxResults).map(it => ({
    title:   it.title   || '',
    link:    it.link    || '',
    snippet: it.snippet || '',
  }));
}

// ─── DuckDuckGo Instant Answer (fallback, sans clé) ──────────────────────────
// API : https://api.duckduckgo.com/?q=<query>&format=json&no_html=1
// Retourne : AbstractText, RelatedTopics[], Infobox
// Pour une bonne couverture, on combine AbstractText + topics + infobox

async function searchViaDDG(query) {
  const proxy = getProxyUrl();
  const ddgUrl = 'https://api.duckduckgo.com/?'
    + new URLSearchParams({ q: query, format: 'json', no_html: '1', skip_disambig: '1' });
  const proxyUrl = proxy + '?url=' + encodeURIComponent(ddgUrl);

  const res = await fetch(proxyUrl, { signal: AbortSignal.timeout(8000) });
  if (!res.ok) throw new Error('DDG erreur HTTP ' + res.status);

  const data = await res.json();
  const results = [];

  // Résultat principal
  if (data.AbstractText) {
    results.push({
      title:   data.Heading || query,
      link:    data.AbstractURL || '',
      snippet: data.AbstractText,
    });
  }

  // Topics liés
  for (const t of (data.RelatedTopics || []).slice(0, 5)) {
    if (t.Text && t.FirstURL) {
      results.push({ title: t.Text.slice(0, 80), link: t.FirstURL, snippet: t.Text });
    }
  }

  // Infobox (clés/valeurs)
  const infobox = data.Infobox?.content || [];
  if (infobox.length > 0) {
    const kvText = infobox.slice(0, 6).map(e => e.label + ' : ' + e.value).join(' | ');
    results.push({ title: 'Infobox — ' + (data.Heading || query), link: '', snippet: kvText });
  }

  return results;
}

// ─── Découpage multi-requêtes (pour les demandes complexes) ──────────────────
//
// Utilise cette fonction quand la requête couvre plusieurs entités distinctes.
// Exemple : "programme TV TF1 France2 France3 Canal+ France5 M6" → 6 requêtes
//   → searchWebMulti(['programme TV TF1', 'programme TV France 2', ...])
//
// Retourne une Map<query, result[]> et un tableau résultats agrégés.

export async function searchWebMulti(queries, { maxResultsPerQuery = 4 } = {}) {
  const resultsMap = {};
  const allResults = [];

  for (const q of queries) {
    try {
      const r = await searchWeb(q, { maxResults: maxResultsPerQuery });
      resultsMap[q] = r;
      allResults.push(...r.map(item => ({ ...item, _query: q })));
    } catch (e) {
      console.warn('[Nestor/search] sous-requête échouée :', q, e.message);
      resultsMap[q] = [];
    }
  }

  return { resultsMap, allResults };
}

// ─── Point d'entrée principal ────────────────────────────────────────────────
//
// Ordre : 1. Serper (si clé + quota ok) → 2. DuckDuckGo Instant Answer
// Retourne : Array<{ title, link, snippet }>

export async function searchWeb(query, { maxResults = 5 } = {}) {
  const hasSerperKey = !!(lsGet('SERPER_KEY') || '').trim();
  const serperDead   = isSerperExhausted();

  if (hasSerperKey && !serperDead) {
    try {
      return await searchViaSerper(query, maxResults);
    } catch (e) {
      if (e.message !== 'SERPER_QUOTA_EXCEEDED') {
        console.warn('[Nestor/search] Serper erreur technique :', e.message);
      }
    }
  }

  // Fallback DuckDuckGo (sans clé)
  try {
    const ddgResults = await searchViaDDG(query);
    if (ddgResults.length > 0) return ddgResults.slice(0, maxResults);
  } catch (e) {
    console.warn('[Nestor/search] DDG erreur :', e.message);
  }

  return [{ title: 'Aucun résultat', link: '', snippet: 'La recherche web est temporairement indisponible.' }];
}

// ─── Statut (pour Réglages) ───────────────────────────────────────────────────

export function getSearchStatus() {
  const hasSerperKey = !!(lsGet('SERPER_KEY') || '').trim();
  const exhausted    = isSerperExhausted();
  if (!hasSerperKey) return { engine: 'duckduckgo', reason: 'Pas de clé Serper — DDG fallback actif' };
  if (exhausted)     return { engine: 'duckduckgo', reason: 'Quota Serper épuisé ce mois — DDG fallback actif' };
  return { engine: 'serper', reason: 'Actif' };
}
