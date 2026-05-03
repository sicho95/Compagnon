# Nestor — Spécification Technique Complète

> Version synchronisée avec le code source — mai 2026

---

## Table des matières

1. [Vue d'ensemble](#1-vue-densemble)
2. [Architecture](#2-architecture)
3. [Structure des fichiers](#3-structure-des-fichiers)
4. [Agents — Catalogue par défaut](#4-agents--catalogue-par-défaut)
5. [Orchestrateur adaptatif](#5-orchestrateur-adaptatif)
6. [Module de recherche web](#6-module-de-recherche-web)
7. [Backends LLM](#7-backends-llm)
8. [Text-to-Speech (TTS)](#8-text-to-speech-tts)
9. [Stockage](#9-stockage)
10. [UI — Dashboard & Companion](#10-ui--dashboard--companion)
11. [Page Réglages](#11-page-réglages)
12. [PWA & Service Worker](#12-pwa--service-worker)
13. [Proxy CORS](#13-proxy-cors)
14. [Clés et secrets](#14-clés-et-secrets)
15. [Roadmap](#15-roadmap)

---

## 1. Vue d'ensemble

**Nestor** est un assistant personnel PWA (Progressive Web App) multi-agents. Il fonctionne entièrement côté client (browser), sans serveur applicatif propre. Toutes les données sont stockées dans `localStorage`. Les appels LLM et de recherche passent par un proxy CORS Cloudflare Worker.

**Principes directeurs :**
- Offline-first : fonctionne sans connexion pour les agents LLM purs (données déjà en mémoire).
- Coût minimal : stratégie d'orchestration priorise toujours l'appel le moins cher qui garantit la qualité.
- Extensible : n'importe quel agent peut être créé à la volée via la Fabrique.
- Transparent : chaque réponse est tracée (étapes d'orchestration visibles en debug).

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        Browser (PWA)                            │
│                                                                 │
│  ┌──────────┐    ┌──────────────────┐    ┌──────────────────┐  │
│  │ companion│    │    dashboard.js   │    │   app.js (init)  │  │
│  │   .js    │    │  (UI principale)  │    │   service-worker │  │
│  └────┬─────┘    └────────┬─────────┘    └──────────────────┘  │
│       │                   │                                     │
│       └──────────┬────────┘                                     │
│                  ▼                                              │
│     ┌────────────────────────────┐                              │
│     │   orchestrator-engine.js   │  ← méta-planificateur        │
│     │   (resolve, quickAnalyze,  │                              │
│     │    coverageScore, runners) │                              │
│     └──────┬─────────────┬───────┘                              │
│            │             │                                      │
│      ┌─────▼──────┐  ┌───▼──────────┐                          │
│      │ backends.js│  │  search.js   │                          │
│      │ (callLLM)  │  │ (searchWeb,  │                          │
│      └─────┬──────┘  │  fetchPage)  │                          │
│            │         └──────┬───────┘                          │
│            │                │                                   │
│  ┌─────────▼────────────────▼──────────────────┐               │
│  │          proxy.sicho95.workers.dev           │               │
│  │          (Cloudflare Worker CORS)            │               │
│  └──────────────────┬──────────────────────────┘               │
│                     │                                           │
└─────────────────────┼───────────────────────────────────────────┘
                      │
          ┌───────────┼───────────┐
          ▼           ▼           ▼
     Groq API    Serper API   DDG API
     OpenRouter  Gemini TTS   Perplexity
```

---

## 3. Structure des fichiers

```
nestor/
├── index.html                  # Point d'entrée PWA
├── manifest.json               # Métadonnées PWA (nom, icônes, thème)
├── service-worker.js           # Cache offline + stratégie réseau
├── css/
│   └── (styles globaux)
└── src/
    ├── app.js                  # Bootstrap : init storage, agents, UI
    ├── api/
    │   ├── backends.js         # callLLM() + routage par backendId
    │   ├── backends.json       # Catalogue backends (Groq, Perplexity, OpenRouter, Puter)
    │   ├── search.js           # searchWeb(), fetchPageText(), searchWebMulti()
    │   └── tts.js              # speak(), stopSpeech(), setSilentMode()
    ├── core/
    │   ├── orchestrator-engine.js  # Méta-planificateur + runners
    │   ├── default-agents.js       # 9 agents par défaut au premier démarrage
    │   └── gardener.js             # Maintenance/compaction des agents
    ├── bt/                     # Bluetooth (futur / WIP)
    ├── device/                 # Capteurs appareil (futur / WIP)
    ├── input/                  # Gestion entrée vocale/texte
    ├── storage/
    │   └── agents-db.js        # CRUD agents localStorage + helpers lsGet/lsSet
    ├── sync/                   # Synchronisation inter-appareils (futur / WIP)
    └── ui/
        ├── companion.js        # Widget flottant (bouton Nestor)
        └── dashboard.js        # UI complète : chat, agents, réglages
```

---

## 4. Agents — Catalogue par défaut

Les agents sont créés au premier démarrage via `defaultAgents()` (src/core/default-agents.js). Chaque agent est un objet JSON persisté dans localStorage.

### Schéma d'un agent

```json
{
  "id": "agent-xxx",
  "name": "Nom",
  "role": "orchestrator | web-analyst | web-search | factory | gardener | generic | <slug-metier>",
  "description": "Une phrase.",
  "tags": ["tag1", "tag2"],
  "backendId": "groq-llama",
  "system_prompt": "...",
  "memory_profile": { "level": "normal | high", "scope": "..." },
  "preferences": [],
  "examples": [],
  "metrics": { "corrections": 0, "confidence": 1, "lastUsed": null },
  "version": 1,
  "createdAt": "ISO",
  "updatedAt": "ISO"
}
```

### Agents système (non supprimables)

| ID | Nom | Rôle | Description |
|---|---|---|---|
| `agent-orchestrateur` | Orchestrateur | `orchestrator` | Analyse, planifie et délègue. Jamais de réponse directe si un agent spécialisé existe. |
| `agent-jardinier` | Jardinier | `gardener` | Nettoie et compacte les system_prompts des agents. Ne répond pas à l'utilisateur. |
| `agent-fabrique` | Fabrique d'agents | `factory` | Crée un agent spécialisé JSON à partir d'un brief court. Force `role: web-analyst` si besoin web détecté. |
| `agent-web-analyst` | Web Analyst | `web-analyst` | Recherche web + synthèse directe. Répond en 1-5 lignes. Une seule source. Jamais de liste de liens. |

### Agents métier par défaut

| ID | Nom | Rôle | Domaine |
|---|---|---|---|
| `agent-mensualites` | Mensualités | `monthly-payments` | Suivi paiements récurrents, tableau Markdown |
| `agent-pea` | PEA | `pea-portfolio` | Suivi portefeuille PEA, PRU, allocation, arbitrages |
| `agent-histoires` | Histoires | `stories` | Histoires interactives, branches, choix `[CHOIX A/B]` |
| `agent-recherche-ciblee` | Recherche ciblée | `research` | Synthèse sans web, cadrage question, bloc "À vérifier" |
| `agent-recherche-web` | Recherche web | `web-search` | Recherche web brute + résumé en 3-5 points + liens utiles |

---

## 5. Orchestrateur adaptatif

**Fichier :** `src/core/orchestrator-engine.js`

### Pipeline de décision (priorité coût croissant)

```
Pré-analyse rapide (quickAnalyze) — 0 appel LLM
    │
    ├─ Score heuristique ≥ 65% ET requête simple ?
    │       └─ FAST-ROUTE → direct_mixed | direct_llm (0 appel LLM supplémentaire)
    │
    └─ Sinon : planification LLM (buildMetaPlanPrompt)
            │
            ├─ direct_mixed   : agent web-analyst existant couvre tout (1 appel)
            ├─ direct_llm     : agent LLM pur existant (1 appel, pas de web)
            ├─ chain          : chaîne d'agents existants, max 3 (N appels séquentiels)
            ├─ create_mixed   : créer agent mixte dédié (1 création + 1 appel)
            ├─ create_agent   : créer agent + chaîner (si aucun agent ne couvre)
            └─ direct_orch    : fallback orchestrateur direct
```

### Heuristiques de classification (quickAnalyze)

**Besoin web + fetch page (`needsDeepContent = true`) :**
- Mots temps-réel : `maintenant`, `ce soir`, `programme`, `horaire`, `météo`, `cours`, `live`, `actualité`…
- Mots de définition : `qu'est`, `définition`, `comment fonctionne`, `qui est`, `raconte`…

**Besoin multi-étapes (`isMultiStep`) :**
- Connecteurs : `puis`, `ensuite`, `compare`, `synthèse`, `d'abord`…
- OU message > 200 caractères

### Fonctions clés

| Fonction | Description |
|---|---|
| `quickAnalyze(msg)` | Retourne `{needsWeb, needsDeepContent, needsRealtime, isMultiStep, wordCount}` |
| `coverageScore(agent, msg, needsWeb)` | Score 0-100 de couverture d'un agent pour une requête |
| `enrichQueryWithDatetime(query)` | Remplace "maintenant"/"aujourd'hui" par la date/heure JS réelle |
| `getNowLabel()` | Retourne "lundi 3 mai 2026 à 14h55" (injecté dans tous les system prompts) |
| `runWebAnalyst(agent, msg, query, ctx, opts)` | Lance Serper/DDG → injecte snippets (+fetch page si `needsDeepContent`) → appel LLM |
| `runLlmAgent(agent, msg, ctx)` | Appel LLM pur avec system_prompt + date/heure |
| `assessConfidence(reply, msg)` | Score 0-1 de confiance sur la réponse produite |
| `resolve(msg, agents, orchAgent, opts)` | Point d'entrée principal, retourne `{reply, trace, newAgent, strategy, confidence}` |

### Escalade automatique

Si `confidence < threshold` après la stratégie choisie → relance via l'orchestrateur direct avec la tentative précédente en contexte.

---

## 6. Module de recherche web

**Fichier :** `src/api/search.js`

### Ordre de priorité

```
1. Serper.dev (primaire)
   → 2 500 req/mois GRATUITES
   → Clé SERPER_KEY dans Réglages
   → Si quota épuisé (HTTP 429) → bascule DDG (marqué pour le mois)

2. DuckDuckGo intelligent (fallback, sans clé)
   → Instant Answer API JSON
   → Fallback HTML scraping si résultat vide
   → Split automatique des requêtes complexes
```

### Split automatique des requêtes complexes

Déclenché si :
- Pattern TV temps-réel détecté (`programme tf1 maintenant`, `grille ce soir`…)
- Requête > 8 mots
- Patterns multi-entités (`X et Y et Z`, énumération de noms propres)

**Stratégie TV :**
- 1 chaîne → requête ciblée avec `site:tf1.fr OR site:telerama.fr OR site:programme-tv.net`
- N chaînes → N requêtes séparées, résultats agrégés

### fetchPageText(url, { maxChars })

Fetch via proxy CORS → supprime scripts/styles/nav/footer → extrait `<article>` > `<main>` > `<body>` → texte brut tronqué à `maxChars` (3000 par défaut, 4000 pour les requêtes TV/temps-réel).

### Exports

| Fonction | Usage |
|---|---|
| `searchWeb(query, {maxResults})` | Recherche principale (Serper → DDG) |
| `searchWebMulti(queries, opts)` | Multi-requêtes parallèles → `{resultsMap, allResults}` |
| `fetchPageText(url, {maxChars})` | Extraction texte d'une page web |
| `getSearchStatus()` | Retourne `{engine, reason}` pour l'UI Réglages |

---

## 7. Backends LLM

**Fichiers :** `src/api/backends.js` + `src/api/backends.json`

### Catalogue backends

| ID | Label | Type | Modèle | Clé requise |
|---|---|---|---|---|
| `groq-llama` | Groq — Llama 3.3 70B (rapide) | openai-compatible | `llama-3.3-70b-versatile` | `GROQ_API_KEY` |
| `groq-llama-free` | Groq — Llama 3.1 8B (gratuit) | openai-compatible | `llama-3.1-8b-instant` | `GROQ_API_KEY` |
| `perplexity-sonar` | Perplexity Sonar | openai-compatible | `sonar` | `PERPLEXITY_API_KEY` |
| `openrouter-qwen-free` | Qwen Free (OpenRouter) | openai-compatible | `qwen/qwen3-coder:free` | `OPENROUTER_API_KEY` |
| `puter-qwen` | Qwen (Puter.js — gratuit sans clé) | puter-qwen | — | ❌ aucune |

### callLLM(backendId, { messages, agentConfig })

- Résout le backend dans `backends.json`
- Lit la clé API depuis `localStorage` (`lsGet(backend.envKey)`)
- Pour `puter-qwen` : utilise `puter.ai.chat()` côté client (gratuit, sans clé)
- Retourne `{ message: { content: string } }`

---

## 8. Text-to-Speech (TTS)

**Fichier :** `src/api/tts.js`

### Stratégie

```
1. Mode silence (setSilentMode(true)) → no-op total
2. Gemini TTS (si engine='gemini' + GEMINI_API_KEY)
   → Modèle : gemini-2.5-flash-preview-tts (free tier)
   → PCM 16-bit 24kHz → AudioContext
   → Voix : Aoede
   → Si échec → bascule silencieuse sur browser
3. Browser speechSynthesis (défaut + fallback Gemini)
   → Voix française auto-sélectionnée
   → Vitesse configurable (LS_TTS_RATE, défaut 1.0)
```

### Clés localStorage TTS

| Clé | Valeur | Description |
|---|---|---|
| `NESTOR_SILENT_MODE` | `'1'` / `'0'` | Mode silence global |
| `NESTOR_TTS_ENGINE` | `'browser'` / `'gemini'` | Moteur TTS actif |
| `NESTOR_TTS_VOICE` | nom de voix | Voix browser sélectionnée |
| `NESTOR_TTS_RATE` | `'0.5'`–`'2.0'` | Vitesse de lecture |

### Exports

| Fonction | Description |
|---|---|
| `speak(text)` | Lit le texte avec le moteur actif |
| `stopSpeech()` | Stoppe immédiatement (browser + Gemini) |
| `setSilentMode(bool)` | Active/désactive le silence |
| `isSilentMode()` | Retourne `true` si silencieux |
| `isSpeechEnabled()` | `true` si le TTS peut produire du son |
| `listBrowserVoices()` | Liste `[{name, lang}]` pour l'UI |
| `getTTSStatus()` | Retourne `{engine, reason}` pour l'UI Réglages |

---

## 9. Stockage

**Fichier :** `src/storage/agents-db.js`

Tout repose sur `localStorage`. Pas d'IndexedDB, pas de serveur.

### Helpers

```js
lsGet(key)          // localStorage.getItem(key)
lsSet(key, value)   // localStorage.setItem(key, value)
```

### CRUD Agents

| Fonction | Description |
|---|---|
| `loadAgents()` | Charge tous les agents. Initialise avec `defaultAgents()` si vide. |
| `saveAgent(agent)` | Sauvegarde / met à jour un agent (upsert par `agent.id`) |
| `deleteAgent(id)` | Supprime un agent |
| `getAgent(id)` | Récupère un agent par ID |

### Clés localStorage applicatives

| Clé | Contenu |
|---|---|
| `NESTOR_AGENTS` | JSON array de tous les agents |
| `GROQ_API_KEY` | Clé API Groq |
| `PERPLEXITY_API_KEY` | Clé API Perplexity |
| `OPENROUTER_API_KEY` | Clé API OpenRouter |
| `GEMINI_API_KEY` | Clé API Gemini (TTS cloud) |
| `SERPER_KEY` | Clé API Serper.dev |
| `SEARCH_PROXY_URL` | URL proxy CORS personnalisé (défaut: `https://proxy.sicho95.workers.dev/`) |
| `SERPER_QUOTA_EXHAUSTED_MONTH` | `{month, year}` — bascule DDG si quota Serper épuisé |
| `NESTOR_SILENT_MODE` | Mode silence TTS |
| `NESTOR_TTS_ENGINE` | Moteur TTS actif (`browser` / `gemini`) |
| `NESTOR_TTS_VOICE` | Voix browser sélectionnée |
| `NESTOR_TTS_RATE` | Vitesse lecture TTS |

---

## 10. UI — Dashboard & Companion

### dashboard.js

Interface principale, organisée en onglets :

| Onglet | Contenu |
|---|---|
| **💬 Chat** | Historique messages, champ saisie, bouton 🔊/🔇, trace d'orchestration (debug) |
| **🤖 Agents** | Liste de tous les agents avec rôle, tags, métriques. Édition, suppression, création via Fabrique |
| **⚙️ Réglages** | Configuration backends LLM, recherche web, TTS (voir section 11) |

**Badges de réponse :**
- `WEB` : réponse issue d'une recherche web
- `LLM` : réponse LLM pure
- Sources cliquables (liens) affichés sous la réponse si présents

### companion.js

Widget flottant (bouton circulaire) positionné en bas à droite. Ouvre/ferme le dashboard. Affiche un indicateur de chargement pendant les appels.

---

## 11. Page Réglages

**Sections actuelles de `renderSettings()` dans dashboard.js :**

### Note générale
- Rappel : toutes les clés sont stockées localement dans le navigateur.
- Lien vers [console.groq.com](https://console.groq.com) pour obtenir une clé.

### Backends LLM
Une carte par backend défini dans `backends.json` :
- Champ clé API (type `password`) avec placeholder (`gsk_...`, `sk-...`…)
- Indicateur ✅ (clé présente) / ⚠️ (clé manquante)
- Sauvegarde auto à la saisie

### 🌐 Recherche Web
- Badge statut moteur actif : 🟢 **Serper.dev** (actif) ou 🔵 **DuckDuckGo** (fallback)
- Description de la stratégie en cours
- Champ `SERPER_KEY` (clé Serper, optionnelle)
- Champ `SEARCH_PROXY_URL` (proxy CORS, défaut `https://proxy.sicho95.workers.dev/`)

> **Note :** La section TTS (moteur, voix, vitesse) et la section Bluetooth sont prévues mais leur intégration dans `renderSettings()` est à compléter.

---

## 12. PWA & Service Worker

**Fichier :** `service-worker.js`

- Stratégie **cache-first** pour les assets statiques (HTML, CSS, JS, icônes)
- Stratégie **network-first** pour les appels API (backends LLM, Serper, DDG) — pas de cache pour les réponses dynamiques
- `manifest.json` : nom "Nestor", icônes, `display: standalone`, `theme_color`

---

## 13. Proxy CORS

**URL par défaut :** `https://proxy.sicho95.workers.dev/`

Cloudflare Worker qui ajoute les headers CORS manquants. Toutes les requêtes sortantes (LLM, Serper, DDG, Gemini TTS, fetchPageText) passent par ce proxy.

**Paramètre :** `?url=<URL encodée>`

Le proxy peut être remplacé par une instance personnelle via le champ `SEARCH_PROXY_URL` dans les Réglages.

---

## 14. Clés et secrets

| Secret | Service | Obligatoire | Où obtenir |
|---|---|---|---|
| `GROQ_API_KEY` | Groq (LLM principal) | ✅ Oui | [console.groq.com](https://console.groq.com) |
| `PERPLEXITY_API_KEY` | Perplexity Sonar | Non | [perplexity.ai/settings/api](https://www.perplexity.ai/settings/api) |
| `OPENROUTER_API_KEY` | OpenRouter (Qwen) | Non | [openrouter.ai/keys](https://openrouter.ai/keys) |
| `GEMINI_API_KEY` | Gemini TTS cloud | Non | [aistudio.google.com](https://aistudio.google.com) |
| `SERPER_KEY` | Recherche Serper.dev | Non (DDG fallback) | [serper.dev](https://serper.dev) |

Toutes les clés sont stockées **uniquement dans `localStorage`** du navigateur. Elles ne transitent jamais vers un serveur Nestor (seulement vers les APIs tierces via le proxy CORS).

---

## 15. Roadmap

### En cours / À compléter
- [ ] Section TTS dans la page Réglages (moteur, voix, vitesse, test)
- [ ] Section Bluetooth dans les Réglages
- [ ] Module `src/bt/` — connexion appareil ESP32/Nestor hardware
- [ ] Module `src/device/` — capteurs (batterie AXP2101, accéléromètre…)
- [ ] Module `src/sync/` — synchronisation inter-appareils
- [ ] Jardinier automatique (déclenchement planifié, pas seulement manuel)

### Améliorations identifiées
- [ ] Export/import des agents (JSON backup)
- [ ] Historique des conversations persisté par agent
- [ ] Métriques enrichies (tokens utilisés, temps de réponse par backend)
- [ ] Support multi-langues (EN, ES) dans les heuristiques de recherche
- [ ] Backend Ollama local (via proxy local)

### Hardware Nestor (device dédié)
- Carte Waveshare ESP32 avec contrôleur batterie **AXP2101**
- Batterie LiPo **AT103030** (3.7V ~450mAh, dimensions 10×30×30mm)
- Communication browser ↔ device : Bluetooth Web API (`src/bt/`)
- Remontée données : niveau batterie, température, état charge
