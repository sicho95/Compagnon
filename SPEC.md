# Nestor Compagnon — Spécification V4

> Version **v4** — mai 2026  
> Basée sur le code réel — état implémenté à la date du commit

---

## Table des matières

1. [Vue d'ensemble](#1-vue-densemble)
2. [PWA Compagnon](#2-pwa-compagnon-sicho95githubionestor)
3. [ESP32 Compagnon](#3-esp32-compagnon-waveshare-amoled-216)
4. [Synchronisation BLE](#4-synchronisation-ble)
5. [Clés API & secrets](#5-clés-api--secrets)
6. [Architecture fichiers](#6-architecture-fichiers)
7. [Prochaines étapes / TODO](#7-prochaines-étapes--todo)

---

## 1. Vision globale

**Nestor** est un système à deux composants :

| Composant | Description |
|---|---|
| **PWA Compagnon** | App web hébergée sur `sicho95.github.io/Nestor/`. Hub d'agents LLM, TTS, radars, bourse, météo, musique, gestion du Compagnon ESP32. Fonctionne entièrement côté navigateur (pas de serveur applicatif). |
| **ESP32 Compagnon** | Firmware sur carte Waveshare AMOLED 2.16" (ESP32-S3). Launcher carousel 4 apps autonome. Reçoit le GPS du téléphone via BLE. L'app Nestor ESP32 est un lanceur Web → PWA. |

**Proxy CORS partagé :** `https://proxy.sicho95.workers.dev/?url=<URL encodée>`  
Utilisé par : LLM, TTS Gemini, Serper, DDG, Lufop, Blitzer, Twelve Data, meteo-concept.

---

## 2. PWA Compagnon (`sicho95.github.io/Nestor/`)

### 2.1 Bootstrap & navigation (`src/app.js`)

Point d'entrée : `main()` — enregistre le SW, init les backends, charge les agents IndexedDB.

**State global :**
```js
{
  view: 'hub' | 'chat' | 'agents' | 'settings' | 'fabrique' | 'edit'
      | 'radar' | 'bourse' | 'meteo' | 'musique' | 'companion',
  agents: [],          // Array d'agents chargés depuis IndexedDB
  activeAgent: null,
  editingAgent: null,
  chatHistory: [],
  menuOpen: false,       // drawer Nestor (vue chat uniquement)
  hubMenuOpen: false,    // drawer Hub global (toutes les vues sauf chat)
  _radarPrevView: 'hub',
  _boursePrevView: 'hub',
  _meteoPrevView: 'hub',
  _musiquePrevView: 'hub',
}
```

**Deux drawers hamburger :**
- **Drawer Hub global** (`hubMenuOpen`) — toutes les vues sauf `chat` — navigation apps, Compagnon ESP32, réglages
- **Drawer Nestor** (`menuOpen`) — vue `chat` uniquement — agents, historiques, réglages API

**Nettoyage vues :** `cleanupRadarView()` et `cleanupMusiqueView()` déclenchés automatiquement dans `renderFrame` quand on quitte les vues `radar` / `musique` via `state._prevView`.

**Service Worker** : cache `nestor-v5`, stratégie cache-first pour les assets locaux. 29 modules précachés.

**Modèle** : monolithique modularisé (une source logique, deux rendus : HTML PWA + LVGL ESP32).

---

### 2.2 App Nestor — Agents LLM

#### Agents par défaut (`src/core/default-agents.js`)

9 agents créés au premier démarrage via `loadAgents()` → `defaultAgents()` si IndexedDB vide :

| ID | Nom | Rôle | Backend |
|---|---|---|---|
| `agent-orchestrateur` | Orchestrateur | `orchestrator` | `groq-llama` |
| `agent-jardinier` | Jardinier | `gardener` | `groq-llama` |
| `agent-fabrique` | Fabrique d'agents | `factory` | `groq-llama` |
| `agent-web-analyst` | Web Analyst | `web-analyst` | `groq-llama` |
| `agent-mensualites` | Mensualités | `monthly-payments` | `groq-llama` |
| `agent-pea` | PEA | `pea-portfolio` | `groq-llama` |
| `agent-histoires` | Histoires | `stories` | `groq-llama` |
| `agent-recherche-ciblee` | Recherche ciblée | `research` | `groq-llama` |
| `agent-recherche-web` | Recherche web | `web-search` | `groq-llama` |

**Schéma agent :**
```json
{
  "id": "agent-xxx",
  "name": "Nom",
  "role": "slug",
  "description": "...",
  "tags": [],
  "backendId": "groq-llama",
  "system_prompt": "...",
  "memory_profile": { "level": "normal|high", "scope": "..." },
  "preferences": [],
  "examples": [],
  "metrics": { "corrections": 0, "confidence": 1, "lastUsed": null },
  "version": 1,
  "createdAt": "ISO",
  "updatedAt": "ISO"
}
```

#### Orchestrateur adaptatif (`src/core/orchestrator-engine.js`)

**Pipeline :**
```
quickAnalyze(userMsg)
  → needsWeb, needsDeepContent, needsRealtime, isMultiStep

coverageScore() → scored agents

si score ≥ 65 % et !isMultiStep → FAST-ROUTE
  → direct_mixed (agent web-analyst)
  → direct_llm   (agent LLM pur)
sinon → buildMetaPlanPrompt() → appel LLM → JSON plan
  → direct_mixed / direct_llm / chain (≤ 3) / create_mixed / create_agent / direct_orch

si confiance < 0.65 → escalade orchestrateur direct
```

**`needsDeepContent`** : déclenche `fetchPageText()` sur la 1ère URL pertinente (max 4000 chars si realtime, 3000 sinon). Activé pour : mots-clés temps-réel (programme TV, météo, cours, news) ET mots-clés définition (qu'est-ce que, qui est, comment fonctionne...).

**`enrichQueryWithDatetime()`** : remplace "maintenant", "aujourd'hui", "ce soir" par la date/heure JS réelle avant la requête de recherche.

#### Stockage (`src/storage/agents-db.js`)

| Mécanisme | Contenu |
|---|---|
| IndexedDB `nestor-agents-v1` | Agents (CRUD : `loadAgents`, `saveAgent`, `deleteAgent`) |
| localStorage | Clés API, historiques, config, cache bourse |

**Historique persisté** :
- `saveChatHistory(agentId, history)` — max 100 messages non-système
- `loadChatHistory(agentId)` — clé `NESTOR_HIST_<agentId>`
- `clearChatHistory(agentId)`

**Export/Import** : `exportAgentsJson()` / `importAgentsJson(json, mergeFn)` avec fusion Jardinier.

---

### 2.3 App Radars (`src/ui/radar-view.js`)

**GPS** : `navigator.geolocation.watchPosition` — `enableHighAccuracy: true, maximumAge: 0, timeout: 5000`

**Sources radars** (via proxy CORS) :
| Source | URL |
|---|---|
| Lufop (primaire) | `https://api.lufop.net/api?format=json&nbr=100&q=lat,lon&m=20&pays=fr` |
| Blitzer.de (secondaire) | `https://cdn2.atudo.net/api/4.0/pois.php?z=9&type=0,1,2,3,4,5,ra,w&box=...` |

**Déduplique** les radars distants < 50m (Haversine).

**Logique alertes :**
- `ALERT_DISTANCE_M = 500` m — entrée zone → bip 1500 Hz 300ms
- Dépassement limite → bip 2000 Hz 400ms
- `ALERT_AFTER_M = 150` — fin d'alerte
- Refresh radars toutes les 90s ou si déplacé > 5 km

**WakeLock écran** : `navigator.wakeLock.request('screen')`

**Audio** : WebAudio API — `OscillatorNode` + `GainNode`, unlock sur premier clic.

**État global singleton `RS`** : survit aux re-renders pour conserver watchId, radars, position.

---

### 2.4 App Météo (`src/ui/meteo-view.js`)

L'App Météo est implémentée côté **ESP32** (voir §3.7) et également côté **PWA** via `meteo-view.js`, accessible depuis le hub (icône 🌤).

**API PWA** : `https://api.meteo-concept.com` via proxy CORS — localisation par coordonnées GPS browser.

---

### 2.5 App Bourse (`src/ui/bourse-view.js`)

**4 actifs fixes** :
| Symbole | Label | Flag |
|---|---|---|
| `CAC40` | CAC 40 | 🇫🇷 |
| `BTC/EUR` | Bitcoin | ₿ |
| `EUR/USD` | EUR/USD | 💱 |
| `XAU/USD` | Or (once) | 🥇 |

**API** : `https://api.twelvedata.com/quote?symbol=...&apikey=...&dp=2` (via proxy)

**Heures marché** : lundi–vendredi 9h–18h heure locale → refresh auto toutes les **90s**.  
Hors marché : utilise le cache `NESTOR_BOURSE_CACHE` (localStorage).

**Formatage prix** : EUR/USD → 4 décimales ; > 1000 → `toLocaleString('fr-FR')` sans décimales ; sinon 2 décimales.

**Sans clé** `TWELVE_DATA_KEY` : affiche un message de configuration avec lien vers twelvedata.com.

---

### 2.6 Module TTS cascade (`src/api/tts.js`)

**Cascade dans l'ordre (chaque erreur → suivant) :**

| # | Provider | Modèle | Clé | Voix défaut FR |
|---|---|---|---|---|
| 1 | Gemini 2.0 Flash TTS | `gemini-2.0-flash-preview-tts` | `GEMINI_API_KEY` | Charon |
| 2 | Gemini 2.5 Flash TTS | `gemini-2.5-flash-preview-tts` | `GEMINI_API_KEY` | Charon |
| 3 | Groq PlayAI TTS | `playai-tts` | `GROQ_API_KEY` | Fritz-PlayAI |
| 4 | Web Speech API | `speechSynthesis` | — | auto voix française |

**Gemini** : PCM 16-bit 24kHz → `AudioContext` → `createBuffer(1, n, 24000)`.  
**Groq PlayAI** : WAV → `decodeAudioData` → `AudioBufferSourceNode`.  
**Web Speech** : `SpeechSynthesisUtterance`, voix française auto-sélectionnée, rate configurable.

**Voix Gemini disponibles** :
`Charon` (grave masculin), `Fenrir` (masculin expressif), `Puck` (masculin léger), `Orus` (masculin profond), `Aoede` (féminin clair), `Kore` (féminin doux), `Zephyr` (féminin lumineux), `Leda` (féminin naturel)

**Voix Groq PlayAI disponibles** :
`Fritz-PlayAI`, `Atlas-PlayAI`, `Angelo-PlayAI`, `Orion-PlayAI`, `Celeste-PlayAI`, `Adelaide-PlayAI`

**Clés localStorage TTS** :

| Clé | Valeur |
|---|---|
| `NESTOR_SILENT_MODE` | `'1'` / `'0'` |
| `NESTOR_TTS_VOICE` | nom voix browser |
| `NESTOR_TTS_VOICE_GEMINI` | nom voix Gemini (défaut `Charon`) |
| `NESTOR_TTS_VOICE_GROQ` | nom voix Groq (défaut `Fritz-PlayAI`) |
| `NESTOR_TTS_RATE` | `'0.5'`–`'2.0'` (défaut `'1.0'`) |

**Endpoint Gemini** (via proxy) :  
`https://generativelanguage.googleapis.com/v1beta/models/<model>:generateContent?key=<key>`  
Body : `{ contents: [{ parts: [{ text }] }], generationConfig: { responseModalities: ['AUDIO'], speechConfig: { voiceConfig: { prebuiltVoiceConfig: { voiceName } } } } }`

**Endpoint Groq PlayAI** (direct, pas de proxy) :  
`POST https://api.groq.com/openai/v1/audio/speech` — `{ model, input, voice, response_format: 'wav' }`

---

### 2.7 Backends LLM (`src/api/backends.js` + `backends.json`)

**Backends statiques (backends.json)** :

| ID | Label | Modèle | Clé |
|---|---|---|---|
| `groq-llama` | Groq — Llama 3.3 70B | `llama-3.3-70b-versatile` | `GROQ_API_KEY` |
| `groq-llama-free` | Groq — Llama 3.1 8B | `llama-3.1-8b-instant` | `GROQ_API_KEY` |
| `perplexity-sonar` | Perplexity Sonar | `sonar` | `PERPLEXITY_API_KEY` |
| `openrouter-qwen-free` | Qwen Free (OpenRouter) | `qwen/qwen3-coder:free` | `OPENROUTER_API_KEY` |
| `puter-qwen` | Qwen (Puter.js — gratuit) | `qwen/qwen-plus` | aucune |

**Chargement dynamique Groq** (`loadGroqModels()`) :  
Au démarrage, si `GROQ_API_KEY` présente → `GET https://api.groq.com/openai/v1/models` → crée un backend `groq-dynamic-<id>` par modèle (exclusion : whisper, distil, guard). Cache mémoire session. `resetGroqModelsCache()` disponible pour forcer le rechargement.

**Endpoint LLM** (openai-compatible) :  
`POST <baseUrl>/chat/completions` — body : `{ model, messages }` — timeout : 30s.

---

### 2.8 Recherche web (`src/api/search.js`)

**Stratégie :**
1. **Serper.dev** (si `SERPER_KEY` présent et quota non épuisé)  
   `POST https://google.serper.dev/search` — `{ q, num, gl: 'fr', hl: 'fr' }`  
   Bascule sur DDG si HTTP 429 → marque quota épuisé pour le mois courant (`SERPER_QUOTA_EXHAUSTED_MONTH`)

2. **DuckDuckGo** (fallback sans clé)  
   a) Instant Answer API : `https://api.duckduckgo.com/?q=...&format=json&no_html=1&skip_disambig=1`  
   b) Si vide → scraping HTML : `https://html.duckduckgo.com/html/?q=...`

**Split automatique requêtes complexes** (requêtes TV, multi-entités, > 8 mots) :  
→ N sous-requêtes → N appels DDG → agrégation  
`searchWebMulti(queries)` pour l'orchestrateur.

**`fetchPageText(url, { maxChars })`** : fetch via proxy, extrait `<article>` / `<main>` / `<body>`, nettoie balises, tronque à `maxChars` (défaut 3000).

---

### 2.9 Vue Compagnon ESP32 (`src/ui/companion.js`)

Vue de gestion du Compagnon, **intégrée dans la navigation** : vue `'companion'` présente dans `safeViews` et accessible depuis le drawer Hub global (section "Compagnon ESP32").

**Sections (accordéon collapsible)** :
1. **WiFi** — scan réseaux + provisioning (ssid/password → BLE WIFI_PROVISION), réseaux sauvegardés
2. **Agents & Sync** — sync bidirectionnelle, last-write-wins par `updatedAt`
3. **Batterie & PMIC** — statut tension/SoC, paramètres AXP2101 (courant/tension charge, alertes), courbe SoC AT103030 1000mAh
4. **Clavier distant** — overlay textarea → BLE TEXT_INPUT → Compagnon
5. **Config** — relais LLM sans WiFi, sync auto agents, push config

**Connexion BLE** : `bleConnect()` via Web Bluetooth → filtre nom `'Nestor'`, service `12345678-...`.  
Auto-setup du relais LLM à la connexion (`setupLlmRelay`).

---

### 2.10 Réglages (`src/ui/dashboard.js` → `renderSettings`)

4 sections :

1. **Backends LLM** — champ password par `envKey` (GROQ, OpenRouter, Perplexity), statut ✅/⚠️
2. **Recherche web** — `SERPER_KEY`, `SEARCH_PROXY_URL` (défaut `https://proxy.sicho95.workers.dev/`)
3. **Bourse** — `TWELVE_DATA_KEY`, lien twelvedata.com
4. **TTS** — cascade providers (affichage disponibilité), voix Gemini, voix Groq, voix browser, slider vitesse 0.5–2.0×, bouton test, toggle mode silence

---

## 3. ESP32 Compagnon (Waveshare AMOLED 2.16")

### 3.1 Hardware

| Composant | Détail |
|---|---|
| SoC | ESP32-S3 (Xtensa LX7 dual-core 240 MHz) |
| Flash | 16 MB (QSPI) |
| PSRAM | 8 MB OPI (OPI 80 MHz) |
| Écran | AMOLED 2.16" QSPI, 480×480 px |
| Driver écran | `Arduino_CO5300` (Arduino_GFX_Library) — rotation hardware = 3 |
| Touch | CST9220 sur I2C — polling pur (pas d'IRQ) |
| PMU | AXP2101 — IRQ FALLING sur GPIO13 |
| IMU | QMI8658 (accéléromètre seul, gyro désactivé) |
| Codec audio | ES8311 (DAC/HP) + ES7210 (ADC/mic) |
| Batterie | AT103030 — LiPo 1000 mAh 3.7 V |

### 3.2 Broches (`compagnon/src/config/pin_config.h`)

| Signal | GPIO | Notes |
|---|---|---|
| LCD_CS | 12 | Chip select QSPI |
| LCD_SCLK | 38 | Horloge QSPI |
| LCD_SDIO0..3 | 4, 5, 6, 7 | Bus QSPI données |
| LCD_RESET / TOUCH_RES | 2 | **Partagé** écran + touch |
| IIC_SDA | 15 | I2C (touch, PMU, IMU) |
| IIC_SCL | 14 | I2C horloge |
| TOUCH_INT | 11 | Non utilisé (polling pur) |
| AXP_INT | 13 | IRQ PMU (FALLING, IRAM_ATTR) |
| BTN_LEFT | 18 | Tile précédente (actif LOW) |
| BTN_RIGHT | 0 | Tile suivante / ouvrir (pin BOOT, actif LOW) |
| ES7210 BCLK | 9 | I2S mic bit clock |
| ES7210 LRCK | 45 | I2S mic word select |
| ES7210 DIN | 10 | I2S mic data in |
| ES7210 MCLK | 16 | I2S mic master clock |
| ES8311 DOUT | 8 | I2S HP data out |
| PA | 46 | Ampli classe-D |

### 3.3 Ordre d'initialisation `setup()` (`compagnon/compagnon.ino`)

```
1. hal_pmu_init()        — Wire I2C, AXP2101 : ALDO1=2800mV (AMOLED), ALDO3=3300mV (logic)
                           IRQ court/long bouton power
2. hal_display_init()    — Reset pin 2 (20ms LOW + 120ms HIGH), CO5300 rotation=3
                           LVGL init, buffers DMA PSRAM 40×480px (double buffer)
                           swap16_buf LE→BE dans flush_cb
3. hal_touch_init()      — Wire.begin, delay 500ms, CST9220 polling : essai 0x1A → 0x15 → 0x5A
                           setSwapXY(true), setMirrorXY(true, false) pour 480×480 rotation=3
4. hal_imu_init()        — QMI8658 : ACC_RANGE_4G, ACC_ODR_62_5Hz, LPF_MODE_2, gyro off
5. ui_status_bar_init()  — Barre 480×36px sur lv_layer_top()
6. wifi_mgr_init()       — WiFiManager non-bloquant, AP "Compagnon_Setup" PSK "compagnon", timeout 180s
7. net_ota_init()        — ArduinoOTA hostname "compagnon" port 3232 PSK "nestor_ota"
8. ble_mgr_init()        — BLE server "Compagnon-Nestor", service GPS GATT
9. orchestrator_init()   — brain_init() (stub)
10. ui_launcher_init()   — Tileview 4 apps, pioche les boutons physiques
11. hal_pmu_set_long_press_cb(ui_power_menu_show)
```

### 3.4 Boucle `loop()` (`compagnon/compagnon.ino`)

```cpp
lv_timer_handler()    // LVGL events (priorité max)
hal_pmu_tick()        // IRQ AXP2101 : court→ON/OFF écran, long→callback
wifi_mgr_tick()       // wm.process() + détection changement connexion
net_ota_tick()        // ArduinoOTA.handle()
ble_mgr_tick()        // no-op (callbacks BLE sont async)
ui_status_bar_tick()  // toutes les 10s : heure NTP + % batterie
hal_imu_tick()        // toutes les 500ms : orientation → rotation LVGL
  → si hal_imu_changed() : lv_display_set_rotation(rot_map[orientation])
orchestrator_tick()   // → brain_tick() → dispatch APP_METEO / APP_MUSIQUE
ui_launcher_btn_tick()// polling boutons BTN_LEFT/BTN_RIGHT (debounce 20ms)
delay(5)
```

**Table rotation IMU → LVGL :**

| IMU orientation | LVGL rotation |
|---|---|
| ORIENT_PORTRAIT | LV_DISPLAY_ROTATION_0 |
| ORIENT_LANDSCAPE_L | LV_DISPLAY_ROTATION_270 |
| ORIENT_PORTRAIT_INV | LV_DISPLAY_ROTATION_180 |
| ORIENT_LANDSCAPE_R | LV_DISPLAY_ROTATION_90 |

### 3.5 HAL — Détails

#### Display (`compagnon/src/hal/display.cpp`)
- `Arduino_ESP32QSPI(LCD_CS=12, LCD_SCLK=38, SDIO0-3=4,5,6,7)`
- `Arduino_CO5300(bus, reset=2, rotation=3, 480, 480, 0, 0, 0, 0)`
- LVGL : `lv_display_create(480, 480)`, `LV_DISPLAY_RENDER_MODE_PARTIAL`
- Buffer : `ps_malloc(480 × 40 × sizeof(lv_color_t))` × 2 (fallback `malloc` si PSRAM indispo)
- `swap16_buf()` dans `flush_cb` : LE→BE avant `gfx->draw16bitBeRGBBitmap`
- `rounder_cb` : aligne les coordonnées sur valeurs paires (contrainte CO5300)

#### Touch (`compagnon/src/hal/touch.cpp`)
- `TouchDrvCSTXXX touch` (SensorLib)
- `touch.setPins(-1, -1)` : mode polling I2C pur — évite le problème pulse INT ~5ms trop court
- Essai adresses : 0x1A → 0x15 → 0x5A
- `touch.setMaxCoordinates(480, 480)`, `setSwapXY(true)`, `setMirrorXY(true, false)`
- `touch_read_cb` enregistré comme `LV_INDEV_TYPE_POINTER`

#### IMU (`compagnon/src/hal/imu.cpp`)
- `SensorQMI8658` — essai `QMI8658_L_SLAVE_ADDRESS` puis `QMI8658_H_SLAVE_ADDRESS`
- ACC : 4G, 62.5 Hz, LPF mode 2 — gyro désactivé
- Lecture toutes les 500ms si `getDataReady()`
- Seuil T = 0.5g pour changement d'orientation
- `hal_imu_changed()` : one-shot flag (reset à chaque lecture)

#### PMU (`compagnon/src/hal/pmu.cpp`)
- `XPowersAXP2101`, IRQ sur GPIO13 (IRAM_ATTR)
- Rails : ALDO1 → 2800 mV (AMOLED), ALDO3 → 3300 mV (touch + logique)
- Appui court : toggle écran (setBrightness 0/200 + displayOff/On)
- Appui long : `_long_cb()` → `ui_power_menu_show()`
- **Veille** (`hal_pmu_enter_sleep`) : light sleep + `esp_sleep_enable_ext0_wakeup(AXP_INT, 0)`  
  Au réveil : restaure MADCTL CO5300 (`gfx_bus->writeC8D8(0x36, 0xA0)`)
- **Arrêt** (`hal_pmu_shutdown`) : `pmu.shutdown()` + fallback `esp_deep_sleep_start()`
- `hal_pmu_battery_pct()` → `pmu.getBatteryPercent()`

### 3.6 Launcher carousel (`compagnon/src/ui/launcher.cpp`)

**4 tiles horizontales (LVGL tileview)** :

| Index | Label | Sous-titre | Couleur BG | Couleur TXT | Icône LVGL |
|---|---|---|---|---|---|
| 0 | Nestor | IA Compagnon | `#0D1B3E` | `#7EB8F7` | `LV_SYMBOL_WIFI` |
| 1 | Radars | Alertes routières | `#0A0A1A` | `#7EB8F7` | `LV_SYMBOL_AUDIO` |
| 2 | Bourse | Marchés & Actifs | `#071A07` | `#66EE88` | `LV_SYMBOL_UP` |
| 3 | Météo | Prévisions | `#0A0E1A` | `#FFCC44` | `LV_SYMBOL_WARNING` |

**Layout d'une carte** : icône 48pt centré à y=-30, titre 24pt à y=+20, sous-titre 12pt 70% opacité à y=+52. Indicateur `x/4` en bas de chaque tile. Card 220×180px, opacité 40%, border radius 20px, shadow 30%.

**Fond écran** : `#050510`

**Boutons physiques** (polling dans `ui_launcher_btn_tick()`, appelé hors LVGL pour éviter la latence 30–100ms de `lv_timer_handler`) :
- BTN_LEFT court → `go_to(cur_idx - 1)`
- BTN_RIGHT court → `go_to(cur_idx + 1)`
- BTN_RIGHT long (800ms) → `open_current()` → lance l'app active
- BTN_LEFT long → non affecté
- Debounce : 20ms

**Menu power** (`ui_power_menu_show()`) — overlay `lv_layer_top()`, 250×210px, 3 boutons :
- **Veille** (`#1A3A1A`) → `hal_pmu_enter_sleep()`
- **Arrêt complet** (`#3A1A1A`) → `hal_pmu_shutdown()`
- **Annuler** (`#1A1A2A`) → ferme l'overlay

### 3.7 Apps ESP32

#### App Nestor (`compagnon/src/apps/nestor/nestor_app.cpp`)

Pas de LLM embarqué. Lance une tâche FreeRTOS (core 0, 4096 bytes stack) :
- Attend `WL_CONNECTED` max 12s
- Si OK : `WebServer` port 80 → redirect 302 vers `https://sicho95.github.io/Nestor/`
- Affiche l'URL pour scanner/cliquer depuis le téléphone
- Si timeout : affiche "WiFi indisponible / NestorOS_Setup"

#### App Radars (`compagnon/src/apps/radars/radar_app.cpp`)

**Stub** — affiche UI placeholder "Scan RF / LoRa — en cours de développement". Bouton retour fonctionnel.

#### App Bourse (`compagnon/src/apps/bourse/bourse_app.cpp`)

**Stub** — affiche UI placeholder "Marchés & Actifs — en cours de développement". Bouton retour fonctionnel.

#### App Météo (`compagnon/src/apps/meteo/meteo_app.cpp`)

**Implémentée.** Dépend de WiFi + GPS BLE.

**APIs** (HTTPClient direct, pas de proxy) :
```
GET https://api.meteo-concept.com/api/location/near
    ?latlng=<lat>,<lon>&token=<API_KEY_METEO>
→ { city: { insee: INT, nom: STRING } }

GET https://api.meteo-concept.com/api/forecast/daily
    ?insee=<code>&token=<API_KEY_METEO>
→ { forecast: [ { datetime, tmin, tmax, weather, probarain }, ... ] }
```

**Position GPS** : `ble_mgr_get_gps()` — défaut Paris (48.8566, 2.3522)

**Affichage** : 3 cartes côte à côte (140×180px, gap 12px, start_x=18), prévisions J+0..J+2 :
date (Zeller), icône LVGL, condition texte, tmin/tmax °C, proba pluie %

**Codes météo meteo-concept** :
- 1–4 → Ensoleillé | 10–16 → Nuageux | 20–26 → Pluie | 40–48 → Forte pluie | 60–78 → Neige | 100–102 → Orage

**Refresh** : toutes les 10min via `meteo_app_tick()` — appelé depuis `brain_tick()` via `orchestrator_get_app()`.

#### Status bar (`compagnon/src/ui/status_bar.cpp`)

Barre fixe 480×36px sur `lv_layer_top()`, refresh toutes les 10s.

- **Gauche** : date + heure `"dd mmm · HH:MM"` (NTP : pool.ntp.org + time.google.com, tz=Europe/Paris DST auto)
- **Droite** : icône BLE (`#7EB8F7` si connecté), icône WiFi (`#00CC44` si connecté), jauge 28×12px, % batterie
- **Couleurs batterie** : > 30% → `#44CC44` ; 15–30% → `#F4A236` ; < 15% → `#F44336`
- NTP déclenché au premier `ui_status_bar_set_wifi(true)`

---

### 3.8 Réseau WiFi (`compagnon/src/system/wifi_mgr.cpp`)

- **WiFiManager** (tzapu), mode non-bloquant (`setConfigPortalBlocking(false)`)
- AP captif : SSID `Compagnon_Setup`, PSK `compagnon`
- Menu captif : wifi, info, sep, exit (sans update firmware ni erase NVS)
- Timeout portail : 180s
- Reconnexion automatique : `wm.process()` dans `wifi_mgr_tick()`
- NTP déclenché via `ui_status_bar_set_wifi(true)` au premier connect

---

### 3.9 OTA (`compagnon/src/net/ota.cpp`)

- **ArduinoOTA** — hostname `compagnon`, port 3232, password `nestor_ota`

---

### 3.10 Bibliothèques Arduino requises

| Bibliothèque | Usage |
|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-1.8 | Board manager — HAL CO5300 |
| LVGL 9.x (Waveshare patched) | UI — `lv_conf.h` dans `compagnon/src/config/` |
| Arduino_GFX_Library | `Arduino_CO5300`, `Arduino_ESP32QSPI` |
| SensorLib (lewisxhe) | `TouchDrvCSTXXX.hpp`, `SensorQMI8658.hpp` |
| XPowersLib | `XPowersAXP2101` |
| WiFiManager (tzapu) | Portail captif |
| ArduinoJson | Parsing HTTP JSON (météo) |
| ESP32 BLE Arduino | `BLEDevice`, `BLEServer`, `BLE2902` |
| ESP32 core built-in | `WiFi`, `HTTPClient`, `WebServer`, `esp_sleep.h` |

**Paramètres de compilation :**

| Paramètre | Valeur |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16 MB |
| Partition Scheme | 16M Flash (3MB APP / 9MB FATFS) |
| PSRAM | OPI PSRAM (OPI 80 MHz) |
| USB Mode | USB CDC On Boot: Enabled |
| Upload Speed | 921600 |
| CPU Frequency | 240 MHz |

**Placement `lv_conf.h`** : `compagnon/src/config/lv_conf.h` (copié depuis la bibliothèque Waveshare LVGL).

---

## 4. Synchronisation BLE

### 4.1 Côté ESP32 — Service GPS GATT (`compagnon/src/net/ble_mgr.cpp`)

| Élément | Valeur |
|---|---|
| Nom device BLE | `Compagnon-Nestor` |
| Service UUID | `4FAFC201-1FB5-459E-8FCC-C5C9C331914B` |
| Caractéristique GPS UUID | `BEB5483E-36E1-4688-B7F5-EA07361B26A8` |
| Propriétés | WRITE + WRITE_NR + NOTIFY |
| Format | `"lat,lon"` — ex : `"48.8566,2.3522"` |

La PWA écrit la position GPS depuis `navigator.geolocation` toutes les secondes. Re-advertising automatique à chaque déconnexion.

Utilisé par : `meteo_app.cpp` (`ble_mgr_get_gps()`)

### 4.2 Côté PWA — Service multi-caractéristiques (`src/bt/ble.js`)

> **Note** : Ce service est implémenté côté PWA mais **pas encore côté ESP32**. Les UUID `12345678-...` sont attendus par la PWA (`companion.js`) mais le firmware n'expose que le service GPS ci-dessus.

| Caractéristique | UUID | Usage |
|---|---|---|
| `WIFI_SCAN` | `12345678-0001-5678-1234-56789abcdef0` | Scan réseaux WiFi |
| `WIFI_PROVISION` | `12345678-0002-5678-1234-56789abcdef0` | Connexion WiFi ssid/password |
| `AGENT_SYNC` | `12345678-0003-5678-1234-56789abcdef0` | Sync agents + config + PMIC |
| `TEXT_INPUT` | `12345678-0004-5678-1234-56789abcdef0` | Clavier distant → ESP32 |
| `LLM_RELAY` | `12345678-0005-5678-1234-56789abcdef0` | Relais LLM ESP32 → téléphone |
| `DEVICE_STATUS` | `12345678-0006-5678-1234-56789abcdef0` | Statut WiFi, batterie, mode |

**Chunking** : écriture par blocs de 512 bytes avec délai 20ms entre chunks.

**Protocole AGENT_SYNC (cmd JSON)** :
```json
{ "cmd": "get_agents" }          → réponse : { "agents": [...] }
{ "cmd": "push", "agents": [...] }
{ "cmd": "config", "config": {...} }
{ "cmd": "get_config" }          → réponse : { "config": {...} }
{ "cmd": "pmic_config", "battery": {...}, "axp": {...} }
{ "cmd": "battery_status" }      → réponse : { "voltage_mv", "soc", "charging", ... }
```

**Sync agents** : last-write-wins par `agent.updatedAt` (ISO string, comparaison timestamp).

**Relais LLM** (`LLM_RELAY`) :
```json
→ ESP32 envoie : { "cmd": "request", "reqId": "...", "messages": [...], "model": "..." }
← PWA répond  : { "cmd": "response", "reqId": "...", "content": "..." }
   ou erreur   : { "cmd": "error", "message": "..." }
```

---

## 5. Clés API & secrets

### 5.1 PWA — localStorage

| Clé | Service | Obligatoire |
|---|---|---|
| `GROQ_API_KEY` | Groq LLM (chat) + Groq PlayAI TTS | Oui (fonctionnement de base) |
| `OPENROUTER_API_KEY` | OpenRouter (Qwen) | Non |
| `PERPLEXITY_API_KEY` | Perplexity Sonar | Non |
| `GEMINI_API_KEY` | Gemini TTS (providers 1 & 2) | Non (TTS dégradé) |
| `SERPER_KEY` | Serper.dev (recherche web primaire) | Non (DDG fallback) |
| `TWELVE_DATA_KEY` | Twelve Data (App Bourse) | Oui (App Bourse) |
| `SEARCH_PROXY_URL` | URL proxy CORS | Non (défaut : `https://proxy.sicho95.workers.dev/`) |

### 5.2 ESP32 — `compagnon/src/config/secrets.h` (gitignorée)

Créer à partir de `secrets.template.h` :

| Constante | Service | Défaut template |
|---|---|---|
| `WIFI_AP_PSK` | Portail captif WiFi | `"compagnon"` |
| `OTA_PASSWORD` | ArduinoOTA | `"nestor_ota"` |
| `API_KEY_GROQ` | Groq LLM | `""` |
| `API_KEY_GEMINI` | Gemini TTS | `""` |
| `API_KEY_METEO` | api.meteo-concept.com | `""` |
| `API_KEY_SERPER` | Serper.dev | `""` |
| `API_KEY_TWELVEDATA` | Twelve Data bourse | `""` |

---

## 6. Architecture fichiers

```
nestor/
├── SPEC.md
├── README.md
├── index.html
├── manifest.json
├── service-worker.js              # Cache nestor-v5, 29 modules précachés
├── css/
│   └── style.css
├── src/
│   ├── app.js                     # Bootstrap + state + deux drawers hamburger
│   ├── api/
│   │   ├── alexa.js               # OAuth Alexa (exchange_code)
│   │   ├── backends.js            # callLLM + chargement dynamique Groq
│   │   ├── backends.json          # 5 backends statiques
│   │   ├── ecovacs.js             # Intégration aspirateur Ecovacs
│   │   ├── search.js              # Serper + DDG + fetchPageText
│   │   ├── stt.js                 # Speech-to-text (Web Speech API)
│   │   └── tts.js                 # Cascade 4 providers TTS
│   ├── bt/
│   │   ├── ble.js                 # Web Bluetooth (service 12345678-...)
│   │   ├── ble_protocol.js        # Protocole haut niveau (WiFi, agents, LLM relay)
│   │   └── ble_status.js          # Store réactif état BLE/device
│   ├── core/
│   │   ├── orchestrator-engine.js # Méta-planificateur adaptatif
│   │   ├── default-agents.js      # 9 agents par défaut
│   │   └── gardener.js            # Compaction agents
│   ├── device/
│   │   ├── device_settings.js     # Config PMIC AXP2101 + courbe SoC AT103030
│   │   └── provisioning.js        # WiFi provisioning via BLE + cache réseaux
│   ├── input/
│   │   └── bt_keyboard.js         # Overlay clavier → ESP32 via TEXT_INPUT
│   ├── storage/
│   │   └── agents-db.js           # IndexedDB agents + lsGet/lsSet
│   ├── sync/
│   │   └── agents_sync.js         # Sync bidirectionnelle agents PWA ↔ ESP32
│   └── ui/
│       ├── dashboard.js           # Chat, agents, settings, radar, bourse, météo, musique
│       ├── companion.js           # Gestion ESP32 (wired — vue 'companion' + drawer Hub)
│       ├── meteo-view.js          # Vue Météo PWA (meteo-concept API)
│       ├── musique-view.js        # Vue Musique PWA
│       ├── radar-view.js          # GPS + Lufop + Blitzer + alertes audio
│       └── bourse-view.js         # Twelve Data, 4 actifs, auto-refresh
└── compagnon/
    ├── compagnon.ino              # Fichier principal Arduino IDE
    ├── src/
    │   ├── apps/
    │   │   ├── app_base.h
    │   │   ├── bourse/
    │   │   │   ├── bourse_app.cpp  # Stub — placeholder UI
    │   │   │   └── bourse_app.h
    │   │   ├── meteo/
    │   │   │   ├── meteo_app.cpp   # Implémenté : HTTPClient + BLE GPS + 3 cartes
    │   │   │   └── meteo_app.h
    │   │   ├── nestor/
    │   │   │   ├── nestor_app.cpp  # Lanceur web → PWA (WebServer redirect)
    │   │   │   └── nestor_app.h
    │   │   └── radars/
    │   │       ├── radar_app.cpp   # Stub — placeholder UI
    │   │       └── radar_app.h
    │   ├── config/
    │   │   ├── lv_conf.h           # Config LVGL 9
    │   │   ├── pin_config.h        # Toutes les broches
    │   │   └── secrets.template.h  # Template secrets (gitignorée : secrets.h)
    │   ├── hal/
    │   │   ├── display.cpp/.h      # CO5300 QSPI + LVGL flush
    │   │   ├── imu.cpp/.h          # QMI8658 orientation
    │   │   ├── pmu.cpp/.h          # AXP2101 alimentation + veille
    │   │   └── touch.cpp/.h        # CST9220 polling I2C
    │   ├── net/
    │   │   ├── ble_mgr.cpp/.h      # BLE serveur GPS (4FAFC201...)
    │   │   └── ota.cpp/.h          # ArduinoOTA
    │   ├── system/
    │   │   ├── brain.cpp/.h        # Dispatch APP_METEO / APP_MUSIQUE via orchestrator
    │   │   ├── orchestrator.cpp/.h # State machine ActiveApp
    │   │   └── wifi_mgr.cpp/.h     # WiFiManager non-bloquant
    │   └── ui/
    │       ├── launcher.cpp/.h     # Carousel 4 apps + menu power
    │       └── status_bar.cpp/.h   # Barre état 36px (heure NTP + batterie)
    └── TestEcran/
        └── TestEcran.ino           # Test écran standalone
```

---

## 7. Prochaines étapes / TODO

### 7.1 ESP32 — Non connecté / stub

| Composant | État | Ce qu'il faut |
|---|---|---|
| `meteo_app_tick()` | Appelé via brain_tick → fetch déclenché, mais dépend du WiFi | S'assurer que le refresh toutes les 10min fonctionne en pratique |
| `bourse_app.cpp` | Stub UI | Appeler Twelve Data via WiFi + HTTPClient |
| `radar_app.cpp` | Stub UI | GPS BLE + fetch Lufop/Blitzer + alertes audio I2S |
| Service BLE multi-chars (`12345678-...`) | **Non implémenté côté ESP32** | Implémenter les 6 caractéristiques GATT côté firmware pour que `companion.js` fonctionne |
| Audio ES8311/ES7210 | Pins définies, bibliothèque non initialisée | Init codec I2S pour TTS + micro |

### 7.2 PWA — Fonctionnel mais non exposé

| Item | État |
|---|---|
| Jardinier automatique | Bouton manuel uniquement dans la vue Agents — pas de planification automatique |
| Export historique conversations | `saveChatHistory` existant mais pas d'export global |
| Métriques enrichies | `metrics.lastUsed` mis à jour ; `corrections` incrémenté par feedback — pas de tableau de bord |
