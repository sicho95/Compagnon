# Nestor — Spécification Technique Complète

> Version synchronisée avec le code source — mai 2026
> Couvre la PWA **et** le Compagnon ESP32

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
16. [Compagnon ESP32 — Matériel](#16-compagnon-esp32--matériel)
17. [Compagnon ESP32 — Architecture logicielle](#17-compagnon-esp32--architecture-logicielle)
18. [Gestion de l'alimentation (PMU)](#18-gestion-de-lalimentation-pmu)
19. [Affichage & Interface LVGL](#19-affichage--interface-lvgl)
20. [Connectivité (WiFi / OTA / BLE)](#20-connectivité-wifi--ota--ble)
21. [Applications V1 — Catalogue](#21-applications-v1--catalogue)
22. [Synchronisation PWA ↔ ESP32](#22-synchronisation-pwa--esp32)
23. [Clés et secrets ESP32](#23-clés-et-secrets-esp32)
24. [Procédure Arduino IDE](#24-procédure-arduino-ide)
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
   → Modèle : gemini-3.1-flash-tts-preview (free tier) puis gemini-2.5-flash-preview-tts (free tier) quand quota epuisé 3.1 epuisé 
   → langue : fr
   → PCM 16-bit 24kHz → AudioContext
   → Voix : Achird ou Sadaltager ou Aoede
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
- Batterie LiPo **AT103030** (3.7V ~1000mAh, dimensions 10×30×30mm)
- Communication browser ↔ device : Bluetooth Web API (`src/bt/`)
- Remontée données : niveau batterie, température, état charge

---

## 16. Compagnon ESP32 — Matériel

### Carte

| Composant | Détail |
|---|---|
| SoC | ESP32-S3 (Xtensa LX7 dual-core 240 MHz) |
| Flash | 16 MB (QSPI) |
| PSRAM | 8 MB OPI (mode OPI_80M) |
| Écran | AMOLED 2.16" QSPI, résolution **480×480** |
| Pilote écran | CO5300 via `Arduino_CO5300` (Arduino_GFX_Library) |
| Touch | CST9220 sur I2C (adresse 0x5A, fallback 0x1A) |
| PMU | AXP2101 (AXP_INT = GPIO13) |
| IMU | QMI8658 |
| Codec audio | ES8311 (DAC/ampli) + ES7210 (ADC/mic) |
| USB | USB-C (USB CDC On Boot Enabled) |

### Broches (pin_config.h)

| Signal | GPIO | Notes |
|---|---|---|
| LCD_CS | 12 | Chip select QSPI |
| LCD_SCLK | 38 | Horloge QSPI |
| LCD_SDIO0..3 | 4, 5, 6, 7 | Bus QSPI données |
| LCD_RESET | 2 | **Partagé avec TOUCH_RES** |
| IIC_SDA | 15 | I2C données (Touch, PMU, IMU) |
| IIC_SCL | 14 | I2C horloge |
| TOUCH_INT | 11 | Interrupt touch |
| AXP_INT | 13 | Interrupt PMU |
| BTN_LEFT | 18 | Bouton physique gauche (prev) |
| BTN_RIGHT | 0 | Bouton physique droit (next + long=ouvrir) |
| AUDIO_BCLK | 48 | I2S bit clock |
| AUDIO_WS | 45 | I2S word select |
| AUDIO_DOUT | 46 | I2S data out (vers ES8311) |
| AUDIO_DIN | 47 | I2S data in (depuis ES7210) |

### Rails d'alimentation AXP2101

| Rail | Tension | Usage |
|---|---|---|
| ALDO1 | 2800 mV | Alimentation AMOLED |
| ALDO3 | 3300 mV | Touch + logique |

---

## 17. Compagnon ESP32 — Architecture logicielle

### Sketch principal

`compagnon/compagnon.ino` — point d'entrée unique Arduino IDE.

**Ordre d'init `setup()`** (critique — ne pas modifier) :
1. `hal_pmu_init()` — rails ALDO1/ALDO3 + IRQ bouton power
2. `hal_display_init()` — reset matériel pin 2, init LVGL
3. `hal_touch_init()` — attente 500 ms post-reset + CST9220
4. `hal_imu_init()` — QMI8658 orientation
5. `ui_status_bar_init()` — barre fixe sur `lv_layer_top()`
6. `wifi_mgr_init()` — portail captif non-bloquant
7. `net_ota_init()` — ArduinoOTA
8. `orchestrator_init()` — planificateur + cerveau
9. `ui_launcher_init()` — carousel 4 apps, charge l'écran
10. `hal_pmu_set_long_press_cb(ui_power_menu_show)` — relie PMU → UI

**Boucle `loop()`** :
```
lv_timer_handler()     // LVGL events UI (priorité maximale)
hal_pmu_tick()         // IRQ AXP2101 short/long press
wifi_mgr_tick()        // maintien connexion + portail
net_ota_tick()         // écoute OTA
ui_status_bar_tick()   // refresh heure + batterie (10 s)
hal_imu_tick()         // orientation
orchestrator_tick()    // cerveau / sync agents
delay(5)
```

### Structure des répertoires

```
compagnon/
├── compagnon.ino           # Sketch principal
└── src/
    ├── config/
    │   ├── pin_config.h    # Broches officielles Waveshare
    │   ├── lv_conf.h       # Config LVGL 9 (copie dans ~/Arduino/libraries/)
    │   └── secrets.h       # Clés API (gitignorée — créer depuis secrets.template.h)
    ├── hal/
    │   ├── display.{h,cpp} # AMOLED CO5300 + LVGL flush + swap16
    │   ├── touch.{h,cpp}   # CST9220 via TouchDrvCSTXXX.hpp
    │   ├── pmu.{h,cpp}     # AXP2101 : batterie, veille, arrêt
    │   └── imu.{h,cpp}     # QMI8658 orientation (future auto-rotation)
    ├── system/
    │   ├── orchestrator.{h,cpp}  # Machine d'état APP_LAUNCHER → APP_*
    │   ├── brain.{h,cpp}         # Planificateur agents (stub V1)
    │   └── wifi_mgr.{h,cpp}      # WiFiManager non-bloquant
    ├── net/
    │   └── ota.{h,cpp}     # ArduinoOTA (hostname: compagnon)
    ├── ui/
    │   ├── status_bar.{h,cpp}  # Barre fixe : heure, BLE, WiFi, batterie
    │   └── launcher.{h,cpp}    # Carousel tileview + menu power overlay
    └── apps/
        ├── app_base.h
        ├── nestor/nestor_app.{h,cpp}
        ├── radars/radar_app.{h,cpp}
        ├── meteo/meteo_app.{h,cpp}
        └── bourse/bourse_app.{h,cpp}
```

---

## 18. Gestion de l'alimentation (PMU)

**Fichier :** `src/hal/pmu.{h,cpp}`

### Comportements bouton power

| Action | Résultat |
|---|---|
| Appui court | écran ON/OFF (rétroéclairage AMOLED) |
| Appui long (>1 s) | Affiche le menu Alimentation (overlay modal) |

### Menu Alimentation (`ui_power_menu_show()`)

Overlay centré sur `lv_layer_top()`, 3 boutons :

| Bouton | Action | Couleur |
|---|---|---|
| Veille | `hal_pmu_enter_sleep()` → light sleep ESP32 | Vert sombre |
| Arrêt complet | `hal_pmu_shutdown()` → coupure AXP2101 | Rouge sombre |
| Annuler | Ferme l'overlay | Bleu sombre |

### Modes d'alimentation

**Veille (light sleep)**
- Écran éteint (`gfx->displayOff()`)
- Reconfiguration IRQ AXP_INT pour réveil sur flanc descendant
- `esp_light_sleep_start()` — RAM et état LVGL préservés
- Réveil sur appui court bouton power → `hal_pmu_screen_on()`

**Arrêt complet**
- Écran éteint, délai 200 ms
- `pmu.shutdown()` (AXP2101 coupe l'alimentation)
- Fallback : `esp_deep_sleep_start()` si shutdown échoue

### Lecture batterie

`int hal_pmu_battery_pct()` — retourne -1 si AXP2101 non disponible.

Affichage status bar :
- > 30% : vert `#44CC44`
- 15–30% : orange `#F4A236`
- < 15% : rouge `#F44336`

---

## 19. Affichage & Interface LVGL

### Configuration LVGL 9

- `LV_COLOR_DEPTH = 16` (RGB565)
- Buffer DMA : 40 lignes × 480 px × 2 octets = 38 400 oct (PSRAM, `MALLOC_CAP_DMA`)
- Double buffer activé pour fluidité maximale
- `rounder_cb` : force les coordonnées de flush à être paires
- `swap16_buf()` avant chaque `draw16bitBeRGBBitmap()` (correction endianness)
- Polices Montserrat activées : 14, 16, 24, 48 (pas 10 ou 12)

### Barre de statut (status_bar.cpp)

Hauteur 45 px, fixée sur `lv_layer_top()` (toujours visible).

| Zone | Contenu | Position |
|---|---|---|
| Gauche | Date + heure (ex: `05 juil 26 · 14:35`) | `LV_ALIGN_LEFT_MID, 8, 0` |
| Droite | Icône BLE → WiFi → jauge batt → % | offsets -114, -90, -56, -8 |

Mise à jour : toutes les 10 secondes dans `ui_status_bar_tick()`.

NTP : configuré à la première connexion WiFi avec TZ Europe/Paris DST auto :
```cpp
setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
tzset();
configTime(0, 0, "pool.ntp.org", "time.google.com");
```

### Launcher — Carousel 4 apps

- `lv_tileview` horizontal, 4 tuiles (APP_COUNT)
- Navigation : bouton gauche (prev), bouton droit court (next), bouton droit long (ouvrir), bouton gauche long (retour)
- Chaque tuile : fond coloré + carte semi-transparente + icône 60 pt + nom 24 pt + sous-titre 12 pt + indicateur `x/4`
- Swipe tactile natif LVGL activé

| App | Icône | Fond | Texte |
|---|---|---|---|
| Nestor | `LV_SYMBOL_WIFI` | `#0D1B3E` | `#7EB8F7` |
| Radars | `LV_SYMBOL_AUDIO` | `#0A0A1A` | `#7EB8F7` |
| Bourse | `LV_SYMBOL_UP` | `#071A07` | `#66EE88` |
| Météo | `LV_SYMBOL_WARNING` | `#0A0E1A` | `#FFCC44` |

---

## 20. Connectivité (WiFi / OTA / BLE)

### WiFi — Portail captif (wifi_mgr.cpp)

- Bibliothèque : WiFiManager (tzapu)
- Mode non-bloquant (`startConfigPortalNonBlocking()`)
- SSID portail : `Compagnon_Setup`, PSK : `compagnon`
- Timeout portail : 180 s, puis abandon
- Menu portail : wifi, info, sep, exit (**pas** de /update ni /erase — sécurité)
- Rappel état connexion → `ui_status_bar_set_wifi(bool)`

### OTA (ota.cpp)

- Bibliothèque : ArduinoOTA (incluse ESP32 Arduino)
- Hostname : `compagnon` (visible dans Arduino IDE → Port réseau)
- Port : 3232 (TCP)
- Mot de passe : `nestor_ota` (défini dans `src/config/secrets.h` V2)
- Progression logguée sur Serial

### BLE (V2)

V1 : non implémenté.
V2 prévu : `src/net/ble_mgr.{h,cpp}`
- Appairage avec l'app PWA (Web Bluetooth API)
- Services GATT : sync agents, GPS, remontée capteurs
- Fallback internet si WiFi indisponible

---

## 21. Applications V1 — Catalogue

### Nestor (nestor_app)

- Interface chat LVGL natif (bulle messages + champ saisie)
- Appel LLM : Groq API (`GROQ_API_KEY` depuis NVS)
- TTS : ES8311 codec pour lecture vocale des réponses
- Micro : ES7210, tap-to-speak, silence automatique si pas de réponse
- Sync agents : déclenché à l'ouverture si WiFi/BLE disponible

### Radars (radar_app)

- Port de la PWA `index.html` (branche sicho95/Radars)
- Alertes routières en temps réel
- GPS : via BLE téléphone
- Données : API Radars (même source que PWA)

### Météo (meteo_app)

- API : `api.meteo-concept.com`
- Clé : `METEO_API_KEY` dans NVS
- Prévisions J+3, affichage pictogrammes + températures

### Bourse (bourse_app)

- API primaire : Twelve Data (`TWELVE_DATA_KEY` dans NVS)
- 4 symboles configurables (défaut : CAC40, BTC, EUR/USD, OR)
- Rafraîchissement : toutes les 1,5 min entre 9h et 18h
- Hors horaires : données figées avec indicateur
- Fallback Finnhub (V2, configurable dans réglages)

---

## 22. Synchronisation PWA ↔ ESP32

### Déclenchement (V1 — BLE non implémenté, prévu V2)

| Événement | Action |
|---|---|
| Ouverture app Nestor (si WiFi LAN disponible) | Sync bidirectionnelle |
| Toutes les 2 heures (si connecté) | Sync bidirectionnelle |
| Modification locale d'un agent | Sync push vers pair |

### Protocole (V2 — BLE GATT)

```
SYNC_HELLO  { version, deviceId, timestamp }
SYNC_PLAN   { agentIds: [...], updatedAt: [...] }
SYNC_DATA   { agents: [...] }   ← envoi des agents manquants/plus récents
```

### Résolution de conflit

**Last-write-wins** par `agent.updatedAt` (timestamp ISO8601). Aucune fusion — l'agent le plus récent écrase l'autre.

### Anti-collision Gardener

Le Gardener (maintenance/compaction agents) tourne de façon autonome sur ESP32 et PWA. Un verrou logiciel (`gardener_lock`) empêche deux instances de tourner simultanément. Priorité au Gardener qui a posé le verrou en premier.

---

## 23. Clés et secrets ESP32

**Fichier :** `src/config/secrets.h` (gitignorée — créer depuis `secrets.template.h`)

| Constante | Service | Obligatoire |
|---|---|---|
| `WIFI_AP_PSK` | Mot de passe portail captif | Oui |
| `OTA_PASSWORD` | Mot de passe OTA Arduino | Oui |
| `GROQ_API_KEY` | LLM Groq (app Nestor) | Oui |
| `GEMINI_API_KEY` | TTS cloud Gemini | Non |
| `METEO_API_KEY` | api.meteo-concept.com | Oui (app Météo) |
| `SERPER_KEY` | Recherche web Serper | Non |
| `TWELVE_DATA_KEY` | Cours boursiers Twelve Data | Oui (app Bourse) |

Stockage à terme : **NVS Preferences** (flash chiffrée ESP32), modifiables depuis l'app Nestor.

---

## 24. Procédure Arduino IDE

### Bibliothèques requises (Gestionnaire de bibliothèques)

| Bibliothèque | Version | Notes |
|---|---|---|
| Waveshare ESP32-S3 AMOLED | latest | Via URL board manager |
| LVGL | 9.x (Waveshare patched) | `lv_conf.h` à copier |
| Arduino_GFX_Library | latest | `Arduino_CO5300` driver |
| SensorLib (lewisxhe) | latest | `TouchDrvCSTXXX.hpp` |
| XPowersLib | 0.3.3 | `XPowersAXP2101` |
| WiFiManager (tzapu) | latest | Portail captif |
| ArduinoOTA | (incluse) | OTA WiFi |

### Placement lv_conf.h

```
~/Documents/Arduino/libraries/lv_conf.h   ← copie depuis src/config/lv_conf.h
```

### Paramètres de compilation

| Paramètre | Valeur |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16 MB |
| Partition Scheme | 16M Flash (3 MB APP/9 MB FATFS) |
| PSRAM | OPI PSRAM (OPI 80 MHz) |
| USB Mode | USB CDC On Boot: Enabled |
| Upload Speed | 921600 |
| CPU Frequency | 240 MHz |

### Premier flash (USB)

1. Brancher ESP32 en USB-C
2. Sélectionner le port COM/série
3. Copier `secrets.template.h` → `secrets.h`, remplir les clés
4. `Croquis → Téléverser`

### Flash OTA (après premier flash)

1. S'assurer que l'ESP32 est connecté au même réseau WiFi
2. Dans Arduino IDE : `Outils → Port → compagnon (ESP32S3 Dev Module)`
3. `Croquis → Téléverser` — le mot de passe OTA sera demandé (`nestor_ota`)

### Débogage

- `Outils → Moniteur série` à 115200 baud
- Tous les modules loguent avec préfixe : `[HAL/DISPLAY]`, `[HAL/TOUCH]`, `[PMU]`, `[OTA]`, `[UI/LAUNCH]`, etc.
