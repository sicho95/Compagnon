import { lsGet, lsSet } from '../storage/agents-db.js';

// ─────────────────────────────────────────────────────────────────────────────
// search.js — Moteur de recherche web pour Nestor
//
// Stratégie (ordre de priorité) :
//
//   1. Serper.dev (primaire)
//      → 2 500 req/mois GRATUITES, vrais résultats Google structurés en JSON
//      → Clé SERPER_KEY à renseigner dans Réglages
//      → Bascule auto sur DDG si quota épuisé (HTTP 429) ou clé absente
//
//   2. DuckDuckGo Instant Answer (fallback final, sans clé)
//      → API publique gratuite, sans inscription
//      → Source faible : pas un vrai SERP complet
//      → RÈGLE DE ROUTAGE :
//          • Requête simple   → 1 appel DDG direct
//          • Requête complexe → décomposition auto en N requêtes simples
//            (ex: "programme TV des 6 chaînes" → 6 requêtes séparées)
//          → Résultats agrégés + normalisés, synthèse finale par le LLM
//
// Multi-requêtes (API publique) :
//   → searchWebMulti(['requête 1', 'requête 2', ...])
//   → Chaque sous-requête lancée indépendamment via searchWeb()
//   → Retourne { resultsMap, allResults } pour synthèse LLM
//
// Proxy CORS : toutes les requêtes passent par proxy.sicho95.workers.dev
// ─────────────────────────────────────────────────────────────────────────────

const DEFAULT_PROXY       = 'https://proxy.sicho95.workers.dev/';
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

// ─── Détection requête complexe ──────────────────────────────────────────────
//
// Heuristiques légères pour détecter si une requête couvre plusieurs entités
// distinctes → déclenchement du split automatique côté DDG.
//
// Indicateurs de complexité :
//   - Plus de 8 mots
//   - Présence de mots de liaison multi-entités (et, ou, ainsi que, +, /)
//   - Énumération détectée (virgules entre noms propres)

const MULTI_ENTITY_PATTERNS = [
  /\bet\b.*\bet\b/i,           // "X et Y et Z"
  /\bou\b.*\bou\b/i,           // "X ou Y ou Z"
  /(?:tf1|france\s*\d|canal\+?|m6|arte|bfm|rmc|itv|bbc|cnn)/i, // chaînes TV
  /[,;]\s*[A-ZÀÂÉÈÊËÎÏÔÙÛÜ]/,  // Énumération de noms propres
];

function isComplexQuery(query) {
  if (query.split(/\s+/).length > 8) return true;
  return MULTI_ENTITY_PATTERNS.some(p => p.test(query));
}

// ─── Décomposition automatique d'une requête complexe ────────────────────────
//
// Stratégie simple et robuste :
//   - Découpe sur ", " / " et " / " ou " pour extraire les entités
//   - Reconstruit des requêtes courtes avec le contexte générique détecté
//   - Ex : "programme TV TF1, France 2 et M6 ce soir"
//       → ["programme TV TF1 ce soir", "programme TV France 2 ce soir", "programme TV M6 ce soir"]

function splitComplexQuery(query) {
  // Extraire entités (split sur virgule, " et ", " ou ")
  const parts = query
    .split(/,|\set\s|\sou\s|\s&\s|\/|\+/i)
    .map(p => p.trim())
    .filter(p => p.length > 1);

  if (parts.length <= 1) return null; // Pas de split possible

  // Contexte = mots communs entre les parties (debut de la requête d'origine)
  // Heuristique : garder le préfixe commun avant la première entité
  const firstEntity = parts[0];
  const prefixMatch = query.indexOf(firstEntity);
  const prefix = prefixMatch > 0 ? query.slice(0, prefixMatch).trim() : '';
  const suffix = query.slice(query.lastIndexOf(parts[parts.length - 1]) + parts[parts.length - 1].length).trim();

  return parts.map(p => [prefix, p, suffix].filter(Boolean).join(' ').trim());
}

// ─── 1. Serper.dev (primaire) ─────────────────────────────────────────────────

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

// ─── 2. DuckDuckGo Instant Answer (fallback, sans clé) ───────────────────────
//
// Source faible — utilisée uniquement sur des requêtes simples.
// Pour les requêtes complexes → voir splitComplexQuery() + searchWebMulti().

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
      title:   data.Heading    || query,
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

// ─── Fallback DDG intelligent (simple ou multi-requêtes) ─────────────────────
//
// Si la requête est complexe : split auto → N appels DDG séparés → agrégation.
// Si la requête est simple   : 1 appel DDG direct.
//
// Retourne toujours Array<{ title, link, snippet, _query? }>

async function searchDDGSmart(query, maxResults) {
  // Tenter le split si requête complexe
  if (isComplexQuery(query)) {
    const subQueries = splitComplexQuery(query);
    if (subQueries && subQueries.length > 1) {
      console.info('[Nestor/search] DDG split :', subQueries.length, 'sous-requêtes pour :', query);
      const allResults = [];
      for (const sq of subQueries) {
        try {
          const r = await searchViaDDG(sq);
          allResults.push(...r.map(item => ({ ...item, _query: sq })));
        } catch (e) {
          console.warn('[Nestor/search] DDG sous-requête échouée :', sq, e.message);
        }
      }
      if (allResults.length > 0) return allResults.slice(0, maxResults * subQueries.length);
    }
  }

  // Requête simple ou split impossible → appel direct
  return await searchViaDDG(query);
}

// ─── Multi-requêtes (API publique pour l'orchestrateur) ──────────────────────
//
// L'orchestrateur passe explicitement un tableau de sous-requêtes.
// Chaque sous-requête passe par searchWeb() (Serper en priorité, DDG sinon).
// Le LLM reçoit allResults avec _query pour la synthèse/reformatage.
//
// Exemple :
//   searchWebMulti([
//     'programme TF1 maintenant',
//     'programme France 2 maintenant',
//     'programme France 3 maintenant',
//     'programme Canal+ maintenant',
//     'programme France 5 maintenant',
//     'programme M6 maintenant',
//   ])

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
// Ordre : 1. Serper (primaire) → 2. DDG intelligent (fallback)
// Le DDG fallback gère automatiquement le split si la requête est complexe.
// Retourne : Array<{ title, link, snippet }>

export async function searchWeb(query, { maxResults = 5 } = {}) {
  // 1. Serper (primaire — si clé présente et quota non épuisé)
  const hasSerperKey = !!(lsGet('SERPER_KEY') || '').trim();
  const serperDead   = isSerperExhausted();

  if (hasSerperKey && !serperDead) {
    try {
      return await searchViaSerper(query, maxResults);
    } catch (e) {
      if (e.message !== 'SERPER_QUOTA_EXCEEDED')
        console.warn('[Nestor/search] Serper erreur technique, bascule DDG :', e.message);
    }
  }

  // 2. DuckDuckGo intelligent (fallback — toujours disponible)
  //    → split automatique si requête complexe
  try {
    const ddgResults = await searchDDGSmart(query, maxResults);
    if (ddgResults.length > 0) return ddgResults.slice(0, maxResults);
  } catch (e) {
    console.warn('[Nestor/search] DDG fallback erreur :', e.message);
  }

  return [{ title: 'Aucun résultat', link: '', snippet: 'La recherche web est temporairement indisponible.' }];
}

// ─── Statut (pour UI Réglages) ────────────────────────────────────────────────

export function getSearchStatus() {
  const hasSerperKey = !!(lsGet('SERPER_KEY') || '').trim();
  const exhausted    = isSerperExhausted();

  if (hasSerperKey && !exhausted) return { engine: 'serper',     reason: 'Serper actif — résultats Google structurés' };
  if (exhausted)                  return { engine: 'duckduckgo', reason: 'Quota Serper épuisé ce mois — DDG fallback actif' };
  return                                 { engine: 'duckduckgo', reason: 'Pas de clé Serper — DDG fallback actif (multi-requêtes auto)' };
}
