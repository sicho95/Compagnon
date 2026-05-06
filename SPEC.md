# Nestor — Spécification Technique Complète

> Version **v2** — mai 2026
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
13. [Proxy CORS](#13-proxy-cors)
14. [Clés et secrets](#14-clés-et-secrets)
15. [Roadmap](#15-roadmap)
16. [Compagnon ESP32 — Matériel](#16-compagnon-esp32--matériel)
17. [Compagnon ESP32 — Architecture logicielle](#17-compagnon-esp32--architecture-logicielle)
18. [Gestion de l'alimentation (PMU)](#18-gestion-de-lalimentation-pmu)
19. [Affichage & Interface LVGL](#19-affichage--interface-lvgl)
20. [Connectivité (WiFi / OTA / BLE)](#20-connectivité-wifi--ota--ble)
21. [Applications V1 — Catalogue](#21-applications-v1--catalogue)
22. [Synchronisation PWA ↔ ESP32](#22-synchronisation-pwa--esp32)
23. [Clés et secrets ESP32](#23-clés-et-secrets-esp32)
24. [Procédure Arduino IDE](#24-procédure-arduino-ide)

---

## 1. Vue d'ensemble

**Nestor** est un assistant personnel PWA (Progressive Web App) multi-agents. Il fonctionne entièrement côté client (browser), sans serveur applicatif propre. Toutes les données sont stockées dans `localStorage` / `IndexedDB`. Les appels LLM et de recherche passent par un proxy CORS Cloudflare Worker.

**Principes directeurs :**
- Offline-first : fonctionne sans connexion pour les agents LLM purs (données déjà en mémoire).
- Coût minimal : stratégie d'orchestration priorise toujours l'appel le moins cher qui garantit la qualité.
- Extensible : n'importe quel agent peut être créé à la volée via la Fabrique.
- Transparent : chaque réponse est tracée (étapes d'orchestration visibles en debug).

**Nouveautés v2 :**
- 🚨 **App Radars** — détection GPS en temps réel, Lufop + Blitzer APIs, alertes audio
- 📈 **App Bourse** — cours en temps réel via Twelve Data, CAC40/BTC/EUR-USD/Or
- 🔊 **Réglages TTS complets** — moteur, voix, vitesse, test, silence
- 💾 **Historique de conversation persisté** par agent (localStorage)
- ⬇️ **Export/Import agents** JSON (déjà présent, confirmé v2)
- 🔑 **TWELVE_DATA_KEY** dans les Réglages

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
│     └──────┬─────────────┬───────┘                              │
│            │             │                                      │
│      ┌─────▼──────┐  ┌───▼──────────┐                          │
│      │ backends.js│  │  search.js   │                          │
│      └─────┬──────┘  └──────┬───────┘                          │
│            │                │                                   │
│  ┌─────────▼────────────────▼──────────────────┐               │
│  │          proxy.sicho95.workers.dev           │               │
│  │          (Cloudflare Worker CORS)            │               │
│  └──────────────────┬──────────────────────────┘               │
│                     │                                           │
│  ┌──────────────────┼────────────────────────┐                  │
│  │  radar-view.js   │  bourse-view.js        │ ← Apps v2        │
│  │  GPS + Lufop     │  Twelve Data API       │                  │
│  └──────────────────┴────────────────────────┘                  │
└─────────────────────┼───────────────────────────────────────────┘
                      │
          ┌───────────┼───────────┐
          ▼           ▼           ▼
     Groq API    Serper API   Lufop API
     OpenRouter  Gemini TTS   Twelve Data
                 Blitzer.de
```

---

## 3. Structure des fichiers

```
nestor/
├── index.html                  # Point d'entrée PWA
├── manifest.json               # Métadonnées PWA (nom, icônes, thème)
├── service-worker.js           # Cache offline (nestor-v4) + stratégie réseau
├── css/
│   └── style.css
└── src/
    ├── app.js                  # Bootstrap + state machine + drawer menu
    ├── api/
    │   ├── backends.js         # callLLM() + routage par backendId
    │   ├── backends.json       # Catalogue backends (Groq, OpenRouter, Puter)
    │   ├── search.js           # searchWeb(), fetchPageText()
    │   └── tts.js              # speak(), stopSpeech(), setSilentMode()
    ├── core/
    │   ├── orchestrator-engine.js  # Méta-planificateur
    │   ├── default-agents.js       # 9 agents par défaut
    │   └── gardener.js             # Maintenance/compaction
    ├── storage/
    │   └── agents-db.js        # CRUD agents + saveChatHistory / loadChatHistory
    └── ui/
        ├── dashboard.js        # UI principale (chat, agents, réglages, radar, bourse)
        ├── radar-view.js       # 🚨 App Radars v2 (GPS + alertes)
        └── bourse-view.js      # 📈 App Bourse v2 (Twelve Data)
```

---

## 4. Agents — Catalogue par défaut

Les agents sont créés au premier démarrage via `defaultAgents()`. Chaque agent est un objet JSON persisté dans IndexedDB.

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

### Agents système

| ID | Nom | Rôle | Description |
|---|---|---|---|
| `agent-orchestrateur` | Orchestrateur | `orchestrator` | Analyse, planifie, délègue. |
| `agent-jardinier` | Jardinier | `gardener` | Nettoie et compacte les prompts. |
| `agent-fabrique` | Fabrique d'agents | `factory` | Crée un agent JSON depuis un brief. |
| `agent-web-analyst` | Web Analyst | `web-analyst` | Recherche web + synthèse 1-5 lignes. |

### Agents métier par défaut

| ID | Nom | Domaine |
|---|---|---|
| `agent-mensualites` | Mensualités | Paiements récurrents |
| `agent-pea` | PEA | Portefeuille bourse |
| `agent-histoires` | Histoires | Histoires interactives |
| `agent-recherche-ciblee` | Recherche ciblée | Synthèse offline |
| `agent-recherche-web` | Recherche web | Web brut + résumé |

---

## 5. Orchestrateur adaptatif

**Fichier :** `src/core/orchestrator-engine.js`

### Pipeline de décision (coût croissant)

```
quickAnalyze() — 0 appel LLM
    ├─ Score ≥ 65% ET requête simple → FAST-ROUTE → direct_mixed | direct_llm
    └─ Sinon : buildMetaPlanPrompt()
            ├─ direct_mixed   : web-analyst existant
            ├─ direct_llm     : agent LLM pur existant
            ├─ chain          : chaîne d'agents (max 3)
            ├─ create_mixed   : créer agent mixte
            ├─ create_agent   : créer + chaîner
            └─ direct_orch    : fallback orchestrateur direct
```

---

## 6. Module de recherche web

**Fichier :** `src/api/search.js`

```
1. Serper.dev (primaire, 2500 req/mois gratuites)
2. SearXNG (fallback illimité, sans clé)
```

Split automatique des requêtes complexes (TV, multi-entités).

---

## 7. Backends LLM

| ID | Label | Modèle | Clé |
|---|---|---|---|
| `groq-llama` | Groq — Llama 3.3 70B | `llama-3.3-70b-versatile` | `GROQ_API_KEY` |
| `groq-llama-free` | Groq — Llama 3.1 8B | `llama-3.1-8b-instant` | `GROQ_API_KEY` |
| `openrouter-qwen-free` | Qwen Free | `qwen/qwen3-coder:free` | `OPENROUTER_API_KEY` |
| `puter-qwen` | Qwen (Puter.js) | — | ❌ aucune |

---

## 8. Text-to-Speech (TTS)

**Fichier :** `src/api/tts.js`

```
1. Mode silence → no-op
2. Gemini TTS (si engine='gemini' + GEMINI_API_KEY)
   → gemini-2.5-flash-preview-tts (free tier)
   → PCM 16-bit 24kHz → AudioContext
3. Browser speechSynthesis (défaut)
   → Voix française auto-sélectionnée
   → Vitesse configurable (NESTOR_TTS_RATE, défaut 1.0)
```

### Clés localStorage TTS

| Clé | Valeur |
|---|---|
| `NESTOR_SILENT_MODE` | `'1'` / `'0'` |
| `NESTOR_TTS_ENGINE` | `'browser'` / `'gemini'` |
| `NESTOR_TTS_VOICE` | nom de voix browser |
| `NESTOR_TTS_RATE` | `'0.5'`–`'2.0'` |

---

## 9. Stockage

**Fichier :** `src/storage/agents-db.js`

Agents : IndexedDB (`nestor-agents-v1`).
Secrets, préférences, historiques : `localStorage`.

### CRUD Agents

| Fonction | Description |
|---|---|
| `loadAgents()` | Charge tous les agents (seed si vide) |
| `saveAgent(agent)` | Upsert avec updatedAt |
| `deleteAgent(id)` | Supprime un agent |
| `exportAgentsJson(agents)` | Sérialise en JSON |
| `importAgentsJson(json, mergeFn)` | Importe + fusionne |

### Historique de conversation (v2)

| Fonction | Description |
|---|---|
| `saveChatHistory(agentId, history)` | Persiste l'historique (hors message système, max 100 messages) |
| `loadChatHistory(agentId)` | Charge l'historique |
| `clearChatHistory(agentId)` | Efface l'historique |

Clé localStorage : `NESTOR_HIST_<agentId>`

### Clés localStorage applicatives

| Clé | Contenu |
|---|---|
| `NESTOR_AGENTS` | JSON array agents (fallback si IndexedDB indispo) |
| `GROQ_API_KEY` | Clé Groq |
| `OPENROUTER_API_KEY` | Clé OpenRouter |
| `GEMINI_API_KEY` | Clé Gemini (TTS cloud) |
| `SERPER_KEY` | Clé Serper.dev |
| `TWELVE_DATA_KEY` | Clé Twelve Data (Bourse v2) |
| `SEARCH_PROXY_URL` | URL proxy CORS |
| `NESTOR_SILENT_MODE` | Mode silence TTS |
| `NESTOR_TTS_ENGINE` | Moteur TTS actif |
| `NESTOR_TTS_VOICE` | Voix browser sélectionnée |
| `NESTOR_TTS_RATE` | Vitesse TTS |
| `NESTOR_BOURSE_CACHE` | Cache derniers cours boursiers |
| `NESTOR_HIST_<id>` | Historique de conversation par agent |

---

## 10. UI — Dashboard & Companion

### Vues disponibles (v2)

| Vue | `state.view` | Description |
|---|---|---|
| Chat | `chat` | Conversation avec l'agent actif |
| Agents | `agents` | Liste + gestion des agents |
| Réglages | `settings` | Clés API, TTS, recherche, bourse |
| Fabrique | `fabrique` | Créer un agent depuis un brief |
| Édition | `edit` | Modifier un agent existant |
| **Radars** | `radar` | 🚨 Surveillance GPS radars (v2) |
| **Bourse** | `bourse` | 📈 Cours en temps réel (v2) |

### Drawer navigation (app.js)

- Orchestrateur (chat direct)
- Agents métier (liste dynamique)
- **Applications** : Radars 🚨, Bourse 📈
- Gérer les agents / Fabrique / Réglages

---

## 11. Page Réglages

### Sections (renderSettings() dans dashboard.js)

1. **Backends LLM** — clés GROQ, OpenRouter (avec statut ✅/⚠️)
2. **🌐 Recherche Web** — SERPER_KEY, proxy CORS, statut moteur
3. **📈 Bourse** — TWELVE_DATA_KEY
4. **🔊 TTS** — moteur (browser/gemini), voix, vitesse, test, mode silence

---

## 12. PWA & Service Worker

**Cache :** `nestor-v4`

**Fichiers précachés :**
- Assets HTML/CSS/JS
- Tous les modules `src/`
- `src/ui/radar-view.js` et `src/ui/bourse-view.js` (v2)

**Stratégie :** cache-first pour les assets locaux, network bypass pour les APIs externes.

---

## 13. Proxy CORS

**URL par défaut :** `https://proxy.sicho95.workers.dev/`

Paramètre : `?url=<URL encodée>`

Utilisé par : LLM, Serper, DDG, Gemini TTS, fetchPageText, Lufop API, Blitzer API, Twelve Data.

---

## 14. Clés et secrets

| Secret | Service | Obligatoire |
|---|---|---|
| `GROQ_API_KEY` | Groq (LLM principal) | ✅ |
| `OPENROUTER_API_KEY` | OpenRouter | Non |
| `GEMINI_API_KEY` | Gemini TTS cloud | Non |
| `SERPER_KEY` | Recherche Serper.dev | Non (DDG fallback) |
| `TWELVE_DATA_KEY` | Cours boursiers | Oui (App Bourse) |

---

## 15. Roadmap

### ✅ Complété en v2
- [x] App Radars GPS (Lufop + Blitzer + alertes audio)
- [x] App Bourse (Twelve Data, 4 symboles, auto-refresh)
- [x] Section TTS complète dans les Réglages
- [x] Historique de conversation persisté par agent
- [x] Export/Import agents JSON
- [x] TWELVE_DATA_KEY dans les Réglages
- [x] Drawer navigation apps v2

### À faire (v3)
- [ ] Module `src/bt/` — connexion BLE ESP32
- [ ] Module `src/device/` — capteurs (batterie, accéléromètre)
- [ ] Module `src/sync/` — synchronisation inter-appareils
- [ ] Jardinier automatique (déclenchement planifié)
- [ ] Historique des conversations exportable
- [ ] Métriques enrichies (tokens, temps de réponse)
- [ ] Support multi-langues (EN, ES)
- [ ] Backend Ollama local

---

## 16. Compagnon ESP32 — Matériel

### Carte

| Composant | Détail |
|---|---|
| SoC | ESP32-S3 (Xtensa LX7 dual-core 240 MHz) |
| Flash | 16 MB (QSPI) |
| PSRAM | 8 MB OPI (mode OPI_80M) |
| Écran | AMOLED 2.16" QSPI, résolution **480×480** |
| Pilote écran | CO5300 via `Arduino_CO5300` |
| Touch | CST9220 sur I2C (adresse 0x5A, fallback 0x1A) |
| PMU | AXP2101 (AXP_INT = GPIO13) |
| IMU | QMI8658 |
| Codec audio | ES8311 (DAC/ampli) + ES7210 (ADC/mic) |

### Broches (pin_config.h)

| Signal | GPIO | Notes |
|---|---|---|
| LCD_CS | 12 | Chip select QSPI |
| LCD_SCLK | 38 | Horloge QSPI |
| LCD_SDIO0..3 | 4, 5, 6, 7 | Bus QSPI données |
| LCD_RESET | 2 | **Partagé avec TOUCH_RES** |
| IIC_SDA | 15 | I2C (Touch, PMU, IMU) |
| IIC_SCL | 14 | I2C horloge |
| TOUCH_INT | 11 | Interrupt touch |
| AXP_INT | 13 | Interrupt PMU |
| BTN_LEFT | 18 | Bouton physique gauche |
| BTN_RIGHT | 0 | Bouton physique droit |
| AUDIO_BCLK | 48 | I2S bit clock |
| AUDIO_WS | 45 | I2S word select |
| AUDIO_DOUT | 46 | I2S data out (ES8311) |
| AUDIO_DIN | 47 | I2S data in (ES7210) |

### Rails d'alimentation AXP2101

| Rail | Tension | Usage |
|---|---|---|
| ALDO1 | 2800 mV | AMOLED |
| ALDO3 | 3300 mV | Touch + logique |

---

## 17. Compagnon ESP32 — Architecture logicielle

### Ordre d'init `setup()` (critique)

1. `hal_pmu_init()` — rails ALDO1/ALDO3 + IRQ bouton power
2. `hal_display_init()` — reset pin 2, init LVGL
3. `hal_touch_init()` — 500 ms post-reset + CST9220
4. `hal_imu_init()` — QMI8658
5. `ui_status_bar_init()` — barre fixe `lv_layer_top()`
6. `wifi_mgr_init()` — portail captif non-bloquant
7. `net_ota_init()` — ArduinoOTA
8. `orchestrator_init()` — planificateur + cerveau
9. `ui_launcher_init()` — carousel 4 apps
10. `hal_pmu_set_long_press_cb(ui_power_menu_show)`

### Boucle `loop()`

```
lv_timer_handler()     // LVGL events (priorité maximale)
hal_pmu_tick()         // IRQ AXP2101
wifi_mgr_tick()        // connexion + portail
net_ota_tick()         // OTA
ui_status_bar_tick()   // refresh 10s
hal_imu_tick()         // orientation
orchestrator_tick()    // cerveau
delay(5)
```

---

## 18. Gestion de l'alimentation (PMU)

| Action bouton power | Résultat |
|---|---|
| Appui court | Écran ON/OFF |
| Appui long (>1 s) | Menu Alimentation (Veille / Arrêt / Annuler) |

**Veille :** light sleep ESP32, RAM et LVGL préservés, réveil sur appui.
**Arrêt :** AXP2101 shutdown, fallback deep sleep.

Batterie : > 30% vert `#44CC44`, 15–30% orange `#F4A236`, < 15% rouge `#F44336`.

---

## 19. Affichage & Interface LVGL

- LVGL 9, `LV_COLOR_DEPTH = 16` (RGB565)
- Buffer DMA : 40 lignes × 480 px (PSRAM, `MALLOC_CAP_DMA`)
- Double buffer, `swap16_buf()` avant flush

### Launcher — Carousel 4 apps

| App | Icône | Couleur fond |
|---|---|---|
| Nestor | `LV_SYMBOL_WIFI` | `#0D1B3E` |
| Radars | `LV_SYMBOL_AUDIO` | `#0A0A1A` |
| Bourse | `LV_SYMBOL_UP` | `#071A07` |
| Météo | `LV_SYMBOL_WARNING` | `#0A0E1A` |

---

## 20. Connectivité (WiFi / OTA / BLE)

### WiFi — Portail captif

- SSID : `Compagnon_Setup`, PSK : `compagnon`
- Timeout : 180 s

### OTA

- Hostname : `compagnon`, port : 3232
- Mot de passe : `nestor_ota`

### BLE (V2 — prévu)

- Services GATT : sync agents, GPS, capteurs

---

## 21. Applications V1 — Catalogue

### Nestor (nestor_app)

- Chat LVGL, Groq API, TTS ES8311, micro ES7210

### Radars (radar_app)

- Port de la PWA `src/ui/radar-view.js`
- GPS via BLE téléphone
- Lufop API + Blitzer.de

### Météo (meteo_app)

- `api.meteo-concept.com` (METEO_API_KEY)
- Prévisions J+3

### Bourse (bourse_app)

- Twelve Data (TWELVE_DATA_KEY)
- 4 symboles : CAC40, BTC, EUR/USD, Or
- Refresh 90s entre 9h–18h, données figées sinon

---

## 22. Synchronisation PWA ↔ ESP32

### Protocole (V2 — BLE GATT)

```
SYNC_HELLO  { version, deviceId, timestamp }
SYNC_PLAN   { agentIds: [...], updatedAt: [...] }
SYNC_DATA   { agents: [...] }
```

Résolution conflits : last-write-wins par `agent.updatedAt`.

---

## 23. Clés et secrets ESP32

**Fichier :** `src/config/secrets.h` (gitignorée)

| Constante | Service |
|---|---|
| `WIFI_AP_PSK` | Portail captif |
| `OTA_PASSWORD` | OTA Arduino |
| `GROQ_API_KEY` | LLM (app Nestor) |
| `GEMINI_API_KEY` | TTS cloud |
| `METEO_API_KEY` | api.meteo-concept.com |
| `SERPER_KEY` | Recherche web |
| `TWELVE_DATA_KEY` | Cours boursiers |

---

## 24. Procédure Arduino IDE

### Bibliothèques requises

| Bibliothèque | Notes |
|---|---|
| Waveshare ESP32-S3 AMOLED | Via URL board manager |
| LVGL 9.x (Waveshare patched) | `lv_conf.h` à copier |
| Arduino_GFX_Library | `Arduino_CO5300` driver |
| SensorLib (lewisxhe) | `TouchDrvCSTXXX.hpp` |
| XPowersLib 0.3.3 | `XPowersAXP2101` |
| WiFiManager (tzapu) | Portail captif |

### Placement lv_conf.h

```
~/Documents/Arduino/libraries/lv_conf.h
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
