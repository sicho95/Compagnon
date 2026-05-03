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
// fetchPageText(url) :
//   → Fetch une page via le proxy CORS, extrait le texte brut utile
//   → Utilisé par runWebAnalyst pour enrichir les réponses au-delà des snippets
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
//   - Pattern chaîne TV détecté (même sur requête courte)

const MULTI_ENTITY_PATTERNS = [
  /\bet\b.*\bet\b/i,           // "X et Y et Z"
  /\bou\b.*\bou\b/i,           // "X ou Y ou Z"
  /(?:tf1|france\s*\d|canal\+?|m6|arte|bfm|rmc|itv|bbc|cnn)/i, // chaînes TV
  /[,;]\s*[A-ZÀÂÉÈÊËÎÏÔÙÛÜ]/,  // Énumération de noms propres
];

// Patterns TV temps-réel : toujours complexe même si < 8 mots
const REALTIME_TV_PATTERN = /\b(programme|programmes|grille|direct|maintenant|actuellement|ce soir|aujourd'hui)\b.*\b(tv|télé|tele|chaîne|chaine|tf1|france\s*\d|m6|canal|arte|bfm)\b|\b(tf1|france\s*\d|m6|canal|arte|bfm)\b.*\b(programme|direct|maintenant|actuellement|ce soir|aujourd'hui)\b/i;

function isComplexQuery(query) {
  if (REALTIME_TV_PATTERN.test(query)) return true;
  if (query.split(/\s+/).length > 8) return true;
  return MULTI_ENTITY_PATTERNS.some(p => p.test(query));
}

// ─── Décomposition automatique d'une requête complexe ────────────────────────
//
// Stratégie :
//   1. Si la requête contient une chaîne TV nommée → split par chaîne
//   2. Sinon split classique sur séparateurs (, et ou &)
//
// Ex : "programme tv tf1 maintenant site:tf1.fr OR site:telerama.fr"
//   → ["programme TV TF1 maintenant site:tf1.fr OR site:telerama.fr"]
//
// Ex : "programme TV TF1, France 2 et M6 ce soir"
//   → ["programme TV TF1 ce soir", "programme TV France 2 ce soir", "programme TV M6 ce soir"]

const TV_CHANNELS = ['TF1', 'France 2', 'France 3', 'France 4', 'France 5', 'M6', 'Canal+', 'Arte', 'BFM TV', 'RMC'];
const TV_CHANNEL_RE = /\b(tf1|france\s*[2-5]|m6|canal\+?|arte|bfm(?:\s*tv)?|rmc)\b/gi;

function splitComplexQuery(query) {
  // Cas spécial : requête TV temps-réel avec UNE seule chaîne
  const tvMatches = [...query.matchAll(TV_CHANNEL_RE)].map(m => m[0]);
  if (tvMatches.length === 1) {
    // Reformuler en requête ciblée avec site: pour forcer des résultats
    const ch = tvMatches[0].replace(/\s+/g, ' ').trim();
    const context = query.replace(TV_CHANNEL_RE, '').replace(/\s+/g, ' ').trim();
    return [`programme TV ${ch} maintenant site:${ch.toLowerCase().replace(/\s|\+/g, '')}.fr OR site:telerama.fr OR site:programme-tv.net`];
  }

  // Cas TV multi-chaînes : générer une requête par chaîne
  if (tvMatches.length > 1) {
    const context = query.replace(TV_CHANNEL_RE, '').replace(/\s+/g, ' ').trim();
    return tvMatches.map(ch => `programme TV ${ch} ${context}`.trim());
  }

  // Split classique sur séparateurs
  const parts = query
    .split(/,|\set\s|\sou\s|\s&\s|\/|\+/i)
    .map(p => p.trim())
    .filter(p => p.length > 1);

  if (parts.length <= 1) return null;

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
// Deux niveaux :
//   a) Instant Answer API (JSON) — rapide, couvre définitions/infos statiques
//   b) Si résultat vide → fallback HTML scraping léger via ?q=...&t=h_
//      (pour les requêtes temps-réel type programme TV)

async function searchViaDDG(query) {
  const proxy  = getProxyUrl();

  // a) Instant Answer API
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

  if (results.length > 0) return results;

  // b) Fallback HTML : scraping léger de la page DuckDuckGo HTML
  //    Retourne les snippets des premiers résultats organiques
  try {
    const htmlUrl  = 'https://html.duckduckgo.com/html/?' + new URLSearchParams({ q: query });
    const htmlProxy = proxy + '?url=' + encodeURIComponent(htmlUrl);
    const htmlRes  = await fetch(htmlProxy, {
      headers: { 'Accept': 'text/html' },
      signal:  AbortSignal.timeout(8000),
    });
    if (!htmlRes.ok) return results;

    const html = await htmlRes.text();

    // Extraire résultats : <a class="result__a" href="...">titre</a> + <a class="result__snippet">snippet</a>
    const linkRe    = /<a[^>]+class="result__a"[^>]+href="([^"]+)"[^>]*>([^<]+)<\/a>/g;
    const snippetRe = /<a[^>]+class="result__snippet"[^>]*>([^<]+)<\/a>/g;

    const links   = [...html.matchAll(linkRe)].slice(0, 5);
    const snippets = [...html.matchAll(snippetRe)].slice(0, 5);

    for (let i = 0; i < links.length; i++) {
      const title   = links[i][2]?.trim()   || query;
      const link    = links[i][1]?.trim()   || '';
      const snippet = snippets[i]?.[1]?.trim() || '';
      if (title || snippet) results.push({ title, link, snippet });
    }
  } catch (e) {
    console.warn('[Nestor/search] DDG HTML fallback échoué :', e.message);
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
  if (isComplexQuery(query)) {
    const subQueries = splitComplexQuery(query);
    if (subQueries && subQueries.length > 0) {
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
      if (allResults.length > 0) return allResults.slice(0, maxResults * Math.max(subQueries.length, 1));
    }
  }

  return await searchViaDDG(query);
}

// ─── Multi-requêtes (API publique pour l'orchestrateur) ──────────────────────
//
// L'orchestrateur passe explicitement un tableau de sous-requêtes.
// Chaque sous-requête passe par searchWeb() (Serper en priorité, DDG sinon).
// Le LLM reçoit allResults avec _query pour la synthèse/reformatage.

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

// ─── fetchPageText — Fetch + extraction texte d'une URL ──────────────────────
//
// Passe par le proxy CORS, récupère le HTML, extrait le texte lisible :
//   1. Supprime scripts, styles, nav, footer, aside
//   2. Extrait le contenu de <article>, <main> ou <body> (priorité décroissante)
//   3. Nettoie le HTML restant → texte brut
//   4. Tronque à maxChars pour ne pas dépasser le contexte LLM
//
// @param {string}  url       — URL à fetcher
// @param {number}  maxChars  — Limite de caractères extraits (défaut : 3000)
// @returns {Promise<string>} — Texte extrait ou chaîne vide si échec

export async function fetchPageText(url, { maxChars = 3000 } = {}) {
  if (!url || !url.startsWith('http')) return '';

  try {
    const proxy    = getProxyUrl();
    const proxyUrl = proxy + '?url=' + encodeURIComponent(url);

    const res = await fetch(proxyUrl, {
      headers: { 'Accept': 'text/html' },
      signal:  AbortSignal.timeout(8000),
    });
    if (!res.ok) return '';

    let html = await res.text();

    // Supprimer les éléments parasites
    html = html
      .replace(/<script[\s\S]*?<\/script>/gi, '')
      .replace(/<style[\s\S]*?<\/style>/gi, '')
      .replace(/<nav[\s\S]*?<\/nav>/gi, '')
      .replace(/<footer[\s\S]*?<\/footer>/gi, '')
      .replace(/<aside[\s\S]*?<\/aside>/gi, '')
      .replace(/<header[\s\S]*?<\/header>/gi, '')
      .replace(/<!--[\s\S]*?-->/g, '');

    // Extraire le bloc principal (article > main > body)
    const contentMatch =
      html.match(/<article[^>]*>([\s\S]*?)<\/article>/i) ||
      html.match(/<main[^>]*>([\s\S]*?)<\/main>/i)       ||
      html.match(/<body[^>]*>([\s\S]*?)<\/body>/i);

    const rawContent = contentMatch ? contentMatch[1] : html;

    // Nettoyer les balises restantes → texte brut
    const text = rawContent
      .replace(/<[^>]+>/g, ' ')
      .replace(/&nbsp;/g, ' ')
      .replace(/&amp;/g,  '&')
      .replace(/&lt;/g,   '<')
      .replace(/&gt;/g,   '>')
      .replace(/&quot;/g, '"')
      .replace(/\s{2,}/g, ' ')
      .trim();

    return text.slice(0, maxChars);
  } catch (e) {
    console.warn('[Nestor/search] fetchPageText échoué pour', url, ':', e.message);
    return '';
  }
}

// ─── Point d'entrée principal ─────────────────────────────────────────────────
//
// Ordre : 1. Serper (primaire) → 2. DDG intelligent (fallback)
// Le DDG fallback gère automatiquement le split si la requête est complexe.
// Retourne : Array<{ title, link, snippet }>

export async function searchWeb(query, { maxResults = 5 } = {}) {
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
