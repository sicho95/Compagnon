import { lsGet, lsSet } from '../storage/agents-db.js';

// ─────────────────────────────────────────────────────────────────────────────
// search.js — Moteur de recherche web pour Nestor
//
// Stratégie (ordre de priorité) :
//   1. SearXNG (instance configurable, open-source, auto-hébergeable)
//      → URL d'instance à renseigner dans Réglages (SEARXNG_URL)
//      → Aucune clé requise, résultats agrégés multi-moteurs
//
//   2. Serper.dev (secondaire, si clé SERPER_KEY configurée)
//      → 2 500 req/mois gratuites, vrais résultats Google structurés
//      → Activé uniquement si SearXNG absent ou en erreur
//      → Bascule auto sur DDG si quota épuisé (HTTP 429)
//
//   3. DuckDuckGo Instant Answer (fallback final, sans clé)
//      → API publique gratuite, fonctionne toujours
//      → Pas un vrai SERP — découper en sous-requêtes simples
//
// Multi-requêtes (requêtes complexes) :
//   → searchWebMulti(['sous-requête 1', 'sous-requête 2', ...])
//   → Chaque requête est lancée indépendamment, résultats agrégés
//   → Le LLM se charge de la synthèse finale
//
// Proxy CORS perso (proxy.sicho95.workers.dev) :
//   → Toutes les requêtes passent par le proxy Cloudflare Worker
// ─────────────────────────────────────────────────────────────────────────────

const DEFAULT_PROXY        = 'https://proxy.sicho95.workers.dev/';
const DEFAULT_SEARXNG_URL  = ''; // Pas d'instance par défaut — à configurer dans Réglages
const LS_SERPER_EXHAUSTED  = 'SERPER_QUOTA_EXHAUSTED_MONTH';

// ─── Helpers ─────────────────────────────────────────────────────────────────

function getProxyUrl() {
  return (lsGet('SEARCH_PROXY_URL') || DEFAULT_PROXY).replace(/\/$/, '');
}

function getSearxngUrl() {
  return (lsGet('SEARXNG_URL') || DEFAULT_SEARXNG_URL).replace(/\/$/, '');
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

// ─── 1. SearXNG ──────────────────────────────────────────────────────────────
// API JSON : GET <instance>/search?q=<query>&format=json&language=fr
// Retourne results[] avec url, title, content

async function searchViaSearxng(query, maxResults) {
  const instanceUrl = getSearxngUrl();
  if (!instanceUrl) throw new Error('SEARXNG_URL non configurée');

  const proxy  = getProxyUrl();
  const target = instanceUrl + '/search?' + new URLSearchParams({
    q:        query,
    format:   'json',
    language: 'fr',
    engines:  'google,bing,brave,ddg',
  });
  const proxyUrl = proxy + '?url=' + encodeURIComponent(target);

  const res = await fetch(proxyUrl, { signal: AbortSignal.timeout(10000) });
  if (!res.ok) throw new Error('SearXNG erreur HTTP ' + res.status);

  const data = await res.json();
  const results = (data.results || []).slice(0, maxResults);
  if (results.length === 0) throw new Error('SearXNG : aucun résultat');

  return results.map(r => ({
    title:   r.title   || '',
    link:    r.url     || '',
    snippet: r.content || '',
  }));
}

// ─── 2. Serper.dev ───────────────────────────────────────────────────────────

async function searchViaSerper(query, maxResults) {
  const apiKey = (lsGet('SERPER_KEY') || '').trim();
  if (!apiKey) throw new Error('SERPER_KEY manquante');

  const proxy    = getProxyUrl();
  const endpoint = 'https://google.serper.dev/search';
  const proxyUrl = proxy + '?url=' + encodeURIComponent(endpoint);

  const res = await fetch(proxyUrl, {
    method:  'POST',
    headers: { 'Content-Type': 'application/json', 'X-API-KEY': apiKey },
    body:    JSON.stringify({ q: query, num: maxResults, gl: 'fr', hl: 'fr' }),
    signal:  AbortSignal.timeout(10000),
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
  const items = (data.organic || []).slice(0, maxResults);
  if (items.length === 0) throw new Error('Serper : aucun résultat organique');

  return items.map(it => ({
    title:   it.title   || '',
    link:    it.link    || '',
    snippet: it.snippet || '',
  }));
}

// ─── 3. DuckDuckGo Instant Answer (fallback final, sans clé) ─────────────────
// API : https://api.duckduckgo.com/?q=<query>&format=json&no_html=1
// Combine AbstractText + RelatedTopics + Infobox

async function searchViaDDG(query) {
  const proxy  = getProxyUrl();
  const ddgUrl = 'https://api.duckduckgo.com/?' + new URLSearchParams({
    q: query, format: 'json', no_html: '1', skip_disambig: '1',
  });
  const proxyUrl = proxy + '?url=' + encodeURIComponent(ddgUrl);

  const res = await fetch(proxyUrl, { signal: AbortSignal.timeout(8000) });
  if (!res.ok) throw new Error('DDG erreur HTTP ' + res.status);

  const data    = await res.json();
  const results = [];

  if (data.AbstractText) {
    results.push({
      title:   data.Heading   || query,
      link:    data.AbstractURL || '',
      snippet: data.AbstractText,
    });
  }

  for (const t of (data.RelatedTopics || []).slice(0, 5)) {
    if (t.Text && t.FirstURL)
      results.push({ title: t.Text.slice(0, 80), link: t.FirstURL, snippet: t.Text });
  }

  const infobox = data.Infobox?.content || [];
  if (infobox.length > 0) {
    const kv = infobox.slice(0, 6).map(e => e.label + ' : ' + e.value).join(' | ');
    results.push({ title: 'Infobox — ' + (data.Heading || query), link: '', snippet: kv });
  }

  return results;
}

// ─── Multi-requêtes (requêtes complexes) ─────────────────────────────────────
//
// Découpe une requête complexe en sous-requêtes indépendantes.
// Exemple : ['programme TV TF1', 'programme TV France 2', ...]
// Retourne { resultsMap: Map<query, result[]>, allResults: result[] }

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

// ─── Point d'entrée principal ─────────────────────────────────────────────────
//
// Ordre : 1. SearXNG → 2. Serper → 3. DuckDuckGo
// Retourne : Array<{ title, link, snippet }>

export async function searchWeb(query, { maxResults = 5 } = {}) {
  // 1. SearXNG (si instance configurée)
  const searxngUrl = getSearxngUrl();
  if (searxngUrl) {
    try {
      return await searchViaSearxng(query, maxResults);
    } catch (e) {
      console.warn('[Nestor/search] SearXNG échoué, bascule Serper :', e.message);
    }
  }

  // 2. Serper (si clé disponible et quota non épuisé)
  const hasSerperKey = !!(lsGet('SERPER_KEY') || '').trim();
  const serperDead   = isSerperExhausted();
  if (hasSerperKey && !serperDead) {
    try {
      return await searchViaSerper(query, maxResults);
    } catch (e) {
      if (e.message !== 'SERPER_QUOTA_EXCEEDED')
        console.warn('[Nestor/search] Serper erreur technique :', e.message);
      // Continue vers DDG
    }
  }

  // 3. DuckDuckGo (fallback final, toujours disponible)
  try {
    const ddgResults = await searchViaDDG(query);
    if (ddgResults.length > 0) return ddgResults.slice(0, maxResults);
  } catch (e) {
    console.warn('[Nestor/search] DDG erreur :', e.message);
  }

  return [{ title: 'Aucun résultat', link: '', snippet: 'La recherche web est temporairement indisponible.' }];
}

// ─── Statut (pour UI Réglages) ────────────────────────────────────────────────

export function getSearchStatus() {
  const searxngUrl   = getSearxngUrl();
  const hasSerperKey = !!(lsGet('SERPER_KEY') || '').trim();
  const exhausted    = isSerperExhausted();

  if (searxngUrl)        return { engine: 'searxng',    reason: 'SearXNG actif — ' + searxngUrl };
  if (hasSerperKey && !exhausted) return { engine: 'serper', reason: 'Serper actif (SearXNG non configuré)' };
  if (exhausted)         return { engine: 'duckduckgo', reason: 'Quota Serper épuisé ce mois — DDG fallback actif' };
  return                        { engine: 'duckduckgo', reason: 'Aucun moteur configuré — DDG fallback actif' };
}
