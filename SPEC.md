# Nestor — Spécification Technique Complète V3

> **Version v3** — mai 2026
> Dual-app system : PWA Compagnon (hub) + 4 apps autonomes (Nestor, Radars, Météo, Bourse)
> ESP32 autonome avec sync bidirectionnelle agents

---

## Table des matières

1. [Vision globale](#1-vision-globale)
2. [Architecture dual-app](#2-architecture-dual-app)
3. [Compagnon PWA — Hub de gestion](#3-compagnon-pwa--hub-de-gestion)
4. [Applications autonomes (Nestor, Radars, Météo, Bourse)](#4-applications-autonomes)
5. [ESP32 Compagnon — Autonome](#5-esp32-compagnon--autonome)
6. [BLE Pairing + Configuration WiFi](#6-ble-pairing--configuration-wifi)
7. [STT/TTS Autonome (ESP32)](#7-stttts-autonome-esp32)
8. [Synchronisation agents PWA ↔ ESP32](#8-synchronisation-agents-pwa--esp32)
9. [Storage PWA](#9-storage-pwa)
10. [Storage ESP32](#10-storage-esp32)
11. [Clés API](#11-clés-api)
12. [Préférences & Toggles apps](#12-préférences--toggles-apps)
13. [Interface LVGL ESP32](#13-interface-lvgl-esp32)
14. [Gestion de l'alimentation ESP32](#14-gestion-de-lalimentation-esp32)
15. [Connectivité ESP32 (WiFi, OTA, BLE)](#15-connectivité-esp32-wifi-ota-ble)
16. [Procédure Arduino IDE](#16-procédure-arduino-ide)
17. [Roadmap](#17-roadmap)

---

## 1. Vision globale

**Nestor** est un système dual composé de :

- **PWA Compagnon** : application hub de gestion, orchestration, configuration des apps. Interface mobile/desktop riche.
- **4 Apps modulaires** : Nestor (IA), Radars (GPS), Météo (prévisions), Bourse (cours). Chaque app existe en version PWA et ESP32.
- **ESP32 Compagnon** : appareil autonome, affichage AMOLED 480×480, carrousel LVGL, sync agents bidirectionnelle.

**Principes** :
- Offline-first : ESP32 fonctionne seul si WiFi/BLE dispo
- Sync autonome : agents s'améliorent des deux côtés (Jardinier PWA + Jardinier ESP32)
- Une source logique : chaque app partagée entre PWA et ESP32 via JSON agents
- Zéro captive portal malveillant : appairage BLE sécurisé + WiFi du téléphone

---

## 2. Architecture dual-app

```
Compagnon PWA (hub)
├── src/apps/
│   ├── nestor/         (agents, STT/TTS, chat UI)
│   ├── radars/         (GPS, alertes radar)
│   ├── meteo/          (prévisions, géoloc)
│   └── bourse/         (cours, tickers)
├── src/core/           (orchestrator, agents-db, storage)
├── src/sync/           (BLE protocol)
├── src/ui/             (Compagnon hub, WiFi config, toggles)
└── ...

Compagnon ESP32 (autonome)
├── compagnon.ino
├── src/apps/
│   ├── nestor/         (LVGL chat, Groq direct, TTS ES8311)
│   ├── radars/         (LVGL écran, API Lufop direct)
│   ├── meteo/          (LVGL affichage, api.meteo-concept direct)
│   └── bourse/         (LVGL écran, Twelve Data direct)
├── src/core/           (orchestrator, agents-db LittleFS)
├── src/net/            (BLE, WiFi)
├── src/ui/             (LVGL launcher, status bar, app screens)
└── src/hal/            (display, touch, PMU, audio)
```

**Modèle** : monolithique modularisé (une source logique, deux rendus : HTML PWA + LVGL ESP32).

---

## 3. Compagnon PWA — Hub de gestion

### Vue d'ensemble

**Compagnon** est la PWA principal. Elle :
- Gère l'appairage BLE (code 6 chiffres aléatoire)
- Configure le WiFi du téléphone → ESP32
- Affiche et bascule les 4 apps disponibles
- Gère les paramètres généraux (fuseau horaire, langue)
- Syncronise agents PWA ↔ ESP32
- Permet d'utiliser chaque app en PWA

### Structure

```
Compagnon-PWA/
├── index.html
├── manifest.json
├── service-worker.js
├── css/
│   └── style.css
└── src/
    ├── app.js                  (bootstrap + état)
    ├── api/
    │   ├── backends.js         (LLM routing)
    │   ├── search.js           (web search)
    │   └── tts.js              (TTS browser/cloud)
    ├── core/
    │   ├── orchestrator-engine.js
    │   ├── default-agents.js
    │   └── gardener.js
    ├── storage/
    │   └── agents-db.js        (IndexedDB agents)
    ├── sync/
    │   └── ble-sync.js         (BLE protocol PWA side)
    ├── ui/
    │   ├── compagnon-hub.js    (hub de gestion, toggles, WiFi config)
    │   ├── apps/
    │   │   ├── nestor-app.js   (chat PWA)
    │   │   ├── radars-app.js   (GPS + alertes)
    │   │   ├── meteo-app.js    (prévisions)
    │   │   └── bourse-app.js   (cours)
    │   ├── settings.js         (clés API, TTS, préfs)
    │   └── ble-pairing.js      (pairing modal)
    └── device/
        └── ble-manager.js      (Web Bluetooth API)
```

### Vues disponibles

| Vue | Description |
|---|---|
| **Hub Compagnon** | Tableau de bord : WiFi, BLE, toggles apps, sync status |
| **Nestor** | Chat avec agents, TTS configurable |
| **Radars** | Carte GPS, alertes radars |
| **Météo** | Prévisions, géolocalisation |
| **Bourse** | Cours en temps réel, portefeuille |
| **Réglages généraux** | Fuseau horaire, langue, clés API |
| **Réglages par app** | Toggles, tickers Bourse, villes Météo, etc. |

---

## 4. Applications autonomes

### 4.1 Nestor — Assistant IA

**PWA** :
- Chat avec agents (Jardinier, Fabrique, Orchestrateur, métier)
- Entrée clavier + Web Speech API (STT in-browser)
- TTS browser speechSynthesis ou Gemini (toggle silence)
- Historique persisté

**ESP32** :
- Chat LVGL natif, écran tactile + boutons physiques
- STT : ES7210 microphone → API gratuite (Google Cloud Speech, ElevenLabs free, etc.)
- TTS : Réponse Nestor → API TTS gratuite (Groq TTS, etc.) → ES8311 codec
- Mode silence : toggle dans paramètres
- Historique en PSRAM (chaud) + LittleFS (froid)

**Agents synchronisés** :
- Agents métier et système partagés via sync bidirectionnelle
- Amélioration Jardinier sur les deux côtés
- Dernière version gagne (last-write-wins updatedAt)

---

### 4.2 Radars — Surveillance GPS

**PWA** :
- Carte Leaflet ou Mapbox Lite
- Position GPS du téléphone (Geolocation API)
- Alertes Lufop + Blitzer.de en temps réel
- Liste des radars proches, alertes audio optionnelles

**ESP32** :
- Position GPS via BLE du téléphone (si appairé + connecté)
- Fallback : dernière position GPS stockée en NVS
- Affichage LVGL : liste radars proches, couleur d'alerte
- API Lufop/Blitzer direct (clés stockées NVS)

**Données partagées** :
- Liste des types d'alertes (radar fixe, radar mobile, section danger)
- Paramètres : rayon recherche, types d'alertes activés/désactivés

---

### 4.3 Météo — Prévisions

**PWA** :
- api.meteo-concept.com (METEO_API_KEY)
- Géolocalisation : GPS téléphone → lat/lon → ville
- Prévisions J+3, carte meteo
- Villes favorites (V2)

**ESP32** :
- Même API (METEO_API_KEY stockée NVS)
- Géolocalisation : priorité
  1. GPS via BLE du téléphone (si appairé)
  2. Dernière position GPS stockée
  3. Fallback Paris
- Affichage LVGL : prévisions J+3, icône météo, temp min/max
- Nom de la ville affiché en haut (ex "Lyon", "Dernière: Nice")

---

### 4.4 Bourse — Cours en temps réel

**PWA** :
- Twelve Data (TWELVE_DATA_KEY) : 4 tickers par défaut
- Finnhub option (FINNHUB_KEY)
- Refresh : 1.5 min (9h–18h), données figées après
- Configuration : ajouter/retirer tickers, alertes prix

**ESP32** :
- Même APIs (clés en NVS)
- 4 tickers configurés (par défaut : CAC40, BTC, EUR/USD, Or)
- Refresh : 1.5 min (Twelve Data), ou 10 sec par ticker (Finnhub) respectant 60 req/min
- Affichage LVGL : prix, variation %, couleur rouge/vert
- Configuration via BLE (input text téléphone)

---

## 5. ESP32 Compagnon — Autonome

### Matériel

| Composant | Détail |
|---|---|
| SoC | ESP32-S3 (Xtensa LX7 dual-core 240 MHz) |
| Flash | 16 MB (QSPI) |
| PSRAM | 8 MB OPI (OPI_80M) |
| Écran | AMOLED 2.16" QSPI, **480×480** |
| Pilote écran | CO5300 via Arduino_CO5300 |
| Touch | CST9220 I2C (0x5A, fallback 0x1A) |
| PMU | AXP2101 (GPIO13 IRQ) |
| IMU | QMI8658 |
| Codec audio | ES8311 (DAC) + ES7210 (ADC/mic) |
| Batterie | 1000 mAh (envisagé) |

### Broches critiques

| Signal | GPIO |
|---|---|
| LCD_RESET | 2 |
| TOUCH_RESET | 2 (partagé) |
| TOUCH_INT | 11 |
| AXP_INT | 13 |
| BTN_LEFT | 18 |
| BTN_RIGHT | 0 |
| IIC_SDA | 15 |
| IIC_SCL | 14 |
| AUDIO_BCLK | 48 |
| AUDIO_WS | 45 |
| AUDIO_DOUT | 46 (ES8311) |
| AUDIO_DIN | 47 (ES7210) |

---

## 6. BLE Pairing + Configuration WiFi

### Workflow complet

#### Étape 1 : Appairage initial

1. **ESP32 boot** → BLE mode (pas AP WiFi)
   - Annonce "Compagnon_XXXX" (XXXX = 4 derniers chiffres MAC)
   - Génère code 6 chiffres aléatoire (stocké en RAM)
   - Affiche le code en haut de l'écran LVGL

2. **Téléphone** → scanne BLE
   - Trouve "Compagnon_XXXX"
   - Appairage : affiche le code 6 chiffres
   - Demande à l'utilisateur de confirmer (ou de saisir le code affiché)
   - Appairage établi

3. **PWA Compagnon** → détecte pairing
   - Affiche "Compagnon appairé" ✅
   - Active section "Configurer WiFi"

#### Étape 2 : Configuration WiFi

**Cas 1 : SSID connu du téléphone + pwd stocké**
- PWA liste les WiFi du téléphone (via Web Bluetooth API, si dispo)
- Utilisateur sélectionne un WiFi
- PWA envoie SSID + PWD via GATT
- ESP32 se connecte, stocke en NVS

**Cas 2 : Nouveau SSID**
- PWA affiche champ texte "Mot de passe"
- Utilisateur colle/saisit le mot de passe (depuis le tel)
- PWA envoie SSID + PWD via GATT
- ESP32 se connecte, stocke en NVS

**Cas 3 : Depuis l'écran ESP32 (fallback)**
- WiFi appairé via BLE dans le tel
- Utilisateur tape long sur l'écran → écran réglages WiFi
- Liste tactile des WiFi connus → boutons Connecter / Oublier
- Pour nouveau WiFi → input text (gros clavier tactile ou BLE del tel)

#### État de l'écran ESP32

| État | Affichage |
|---|---|
| Boot, pas de BLE | WiFi gris ❌, BLE gris ❌, "Appairez via BLE" |
| BLE appairé, pas de WiFi | WiFi gris ❌, BLE bleu ✅, "Configurer WiFi" |
| WiFi connecté | WiFi vert ✅, BLE bleu ✅ (si appairé) |
| WiFi + PWA sync actif | Animation sync en cours |

---

## 7. STT/TTS Autonome (ESP32)

### STT (Speech-to-Text)

**Micro ES7210** → WAV/PCM 16-bit 16 kHz → API STT gratuite

Options recommandées (gratuit) :
1. **Google Cloud Speech** (300 requêtes/mois gratuites)
2. **ElevenLabs free tier** (10,000 caractères/mois)
3. **Groq STT** (gratuit, si API key Groq dispo)

Implémentation :
- Appui long sur bouton micro (gros bouton tactile)
- Enregistrement audio ES7210 en streaming (max 30s)
- Upload WAV à l'API
- Réponse texte → Nestor LLM
- Réponse TTS → jouer audio

### TTS (Text-to-Speech)

**Réponse Nestor** → API TTS gratuite → MP3/WAV → ES8311 codec

Options recommandées (gratuit) :
1. **Groq TTS** (gratuit, intégré à la clé Groq)
2. **ElevenLabs free** (10,000 caractères/mois)
3. **Google Cloud TTS** (500 requêtes/mois gratuites)
4. **Tacotron 2 / Glow-TTS local** (si PSRAM suffisant)

Implémentation :
- API TTS retourne MP3 ou WAV
- Stream en chunks via BLE/WiFi (si PSRAM limité)
- Ou stocke en PSRAM (max 8 MB) si court
- I2S → ES8311 codec → haut-parleur intégré

### Mode silence

- Toggle dans paramètres app Nestor
- Si activé : aucun TTS, texte affiché seul
- Stocké en NVS, synchronisé via BLE à la PWA

---

## 8. Synchronisation agents PWA ↔ ESP32

### Triggers

1. **Au lancement app Nestor** (BLE ou WiFi LAN)
   - Échange : SYNC_HELLO → SYNC_PLAN → SYNC_DATA
   - Résolution conflits : last-write-wins par `updatedAt`

2. **À chaque création/modification d'agent** (local)
   - Push immédiat à l'autre côté (via BLE/WiFi)
   - Flag dirty jusqu'à ACK

3. **Polling périodique** : toutes les 2–3 h
   - Batterie : limite à 1–2 requêtes par jour (ultra-léger)
   - Évite une perte de sync silencieuse

### Protocole GATT

**Caractéristiques** :

| UUID | Direction | Contenu |
|---|---|---|
| `SYNC_HELLO` | ↔ | version, deviceId, timestamp |
| `SYNC_PLAN` | ↔ | agentIds[], updatedAt[] |
| `SYNC_DATA` | ↔ | agents[] JSON complets |

**Handshake** :
```
Device A (ESP32) → SYNC_HELLO { version: 1, deviceId: "esp32-xxx", timestamp: 1715076000 }
Device B (PWA) → SYNC_HELLO { version: 1, deviceId: "pwa-xxx", timestamp: 1715076001 }
Device A → SYNC_PLAN { agentIds: ["a1", "a2"], updatedAt: [1715000000, 1715010000] }
Device B → SYNC_PLAN { agentIds: ["a1", "a2", "a3"], updatedAt: [1715002000, 1715012000, 1715020000] }
→ Merge : Device A ajoute a3, Device B met à jour a1 si updatedAt > sienne
→ Device A → SYNC_DATA { agents: [a1, a2, a3] }
Device B → SYNC_DATA { agents: [a1, a2, a3] }
```

### Résolution de conflits

**Stratégie** : last-write-wins
- Chaque agent a `updatedAt` (ISO timestamp)
- Si deux versions d'un agent existent : la plus récente (updatedAt plus tard) gagne
- Version perdue : archive en `agents-history.json` pour audit

---

## 9. Storage PWA

### IndexedDB

**Base** : `nestor-agents-v1`
**Stores** :
- `agents` : agents avec schéma complet
- `chat-history` : historiques par agentId

### localStorage

| Clé | Contenu |
|---|---|
| `GROQ_API_KEY` | Groq LLM |
| `OPENROUTER_API_KEY` | OpenRouter |
| `GEMINI_API_KEY` | Gemini TTS |
| `SERPER_KEY` | Serper.dev recherche |
| `TWELVE_DATA_KEY` | Bourse |
| `FINNHUB_KEY` | Bourse option |
| `METEO_API_KEY` | Météo Concept |
| `NESTOR_SILENT_MODE` | TTS silence toggle |
| `NESTOR_TTS_ENGINE` | browser / gemini |
| `NESTOR_BOURSE_CACHE` | Cache derniers cours |
| `NESTOR_TIMEZONE` | Europe/Paris (défaut) |
| `NESTOR_LANGUAGE` | fr / en |

---

## 10. Storage ESP32

### NVS Preferences (chiffré)

| Clé | Contenu | Type |
|---|---|---|
| `wifi_ssid` | SSID dernier WiFi | string |
| `wifi_psk` | PWD dernier WiFi | string |
| `wifi_list` | JSON liste WiFi connus | blob |
| `ble_code` | Code 6 chiffres | int |
| `ble_paired` | Paired status | uint8 |
| `GROQ_API_KEY` | Groq LLM | string |
| `GEMINI_API_KEY` | Gemini TTS | string |
| `METEO_API_KEY` | Météo Concept | string |
| `TWELVE_DATA_KEY` | Twelve Data | string |
| `FINNHUB_KEY` | Finnhub option | string |
| `SERPER_KEY` | Serper recherche | string |
| `nestor_silent_mode` | TTS silence | uint8 |
| `bourse_tickers` | JSON tickers | blob |
| `meteo_cities` | JSON villes favorites | blob |
| `radar_radius` | Rayon recherche (km) | int |
| `radar_alerts` | JSON types d'alertes | blob |
| `last_gps_lat` | Dernière latitude | float |
| `last_gps_lon` | Dernière longitude | float |
| `timezone` | POSIX TZ string | string |
| `language` | fr / en | string |

### LittleFS

```
/agents/
├── agent-xxx.json          (agents individuels)
├── agent-yyy.json
└── ...

/sync/
├── agents-history.json     (versions perdues)
└── sync-log.json           (audit sync)

/storage/
├── bourse-cache.json       (derniers cours)
├── meteo-cache.json        (dernière météo)
└── ...
```

Charge agents en PSRAM au boot (chaud), sync LittleFS à la demande (froid).

---

## 11. Clés API

### Source unique : stockage sécurisé

| Clé | Service | Obligatoire | Stockage |
|---|---|---|---|
| `GROQ_API_KEY` | Groq LLM (Nestor) | ✅ | PWA localStorage + NVS ESP32 |
| `GEMINI_API_KEY` | Gemini TTS | ❌ | PWA localStorage + NVS ESP32 |
| `METEO_API_KEY` | api.meteo-concept.com | ✅ (Météo) | PWA localStorage + NVS ESP32 |
| `TWELVE_DATA_KEY` | Twelve Data (Bourse) | ✅ (Bourse) | PWA localStorage + NVS ESP32 |
| `FINNHUB_KEY` | Finnhub (Bourse option) | ❌ | PWA localStorage + NVS ESP32 |
| `SERPER_KEY` | Serper.dev (recherche) | ❌ (DDG fallback) | PWA localStorage + NVS ESP32 |

**Flux** :
1. Utilisateur rentre les clés dans PWA Compagnon (Réglages)
2. Stockées en localStorage PWA
3. Au premier BLE pairing : PWA → ESP32 via GATT
4. ESP32 stocke en NVS chiffré
5. Mise à jour clé PWA : push automatique à l'ESP32 à la prochaine sync
6. Mise à jour clé ESP32 : remontée à la PWA à la prochaine sync

---

## 12. Préférences & Toggles apps

### Compagnon PWA — Réglages généraux

| Paramètre | Stockage | Valeur défaut |
|---|---|---|
| Fuseau horaire | localStorage | Europe/Paris (POSIX TZ) |
| Langue | localStorage | fr |
| Clés API | localStorage | (vides) |
| BLE pairing | localStorage | (device list) |

### Toggles apps

| App | Toggle | Stockage | Défaut |
|---|---|---|---|
| Nestor | Actif / Inactif | localStorage + NVS | ✅ |
| Radars | Actif / Inactif | localStorage + NVS | ✅ |
| Météo | Actif / Inactif | localStorage + NVS | ✅ |
| Bourse | Actif / Inactif | localStorage + NVS | ✅ |

**Sync** : toggles synchronisés via SYNC_DATA (options appliquée sur les deux côtés).

### Préférences par app

#### Nestor
- TTS mode : silence on/off
- Historique : sauvegardé par agentId

#### Radars
- Rayon recherche : 50 km (configurable)
- Types d'alertes : radar fixe, mobile, section danger (toggles)

#### Météo
- Villes favorites : Paris, Lyon, etc. (V2)
- Unités : °C (défaut), °F (option)

#### Bourse
- Tickers à suivre : CAC40, BTC, EUR/USD, Or (modifiable)
- Raffraîchissement : 1.5 min (Twelve Data), 10 sec (Finnhub)

---

## 13. Interface LVGL ESP32

### Architecture UI

- `lv_layer_top()` → status bar fixe
- `lv_tileview` → carousel 4 apps (horizontal swipe)
- Boutons physiques GPIO0/GPIO18 → navigation carousel

### Status bar

Affichage permanent en haut (20 px hauteur) :
```
[Time: 14:35] [WiFi 📶] [BLE 📡] [Battery 75%] [Sync ↔]
```

Couleurs :
- WiFi vert (connecté) / gris (déconnecté)
- BLE bleu (appairé) / gris (non appairé)
- Batterie vert >30%, orange 15–30%, rouge <15%
- Sync animation lors d'une synchronisation

### Carousel 4 apps

| Écran | App | Conteneur |
|---|---|---|
| 0 | Launcher | Liste apps avec icônes |
| 1 | Nestor | Chat LVGL |
| 2 | Radars | Liste radars proches |
| 3 | Météo | Prévisions J+3 |
| 4 | Bourse | Tableau tickers + cours |

**Navigation** :
- Swipe gauche/droit
- Bouton LEFT (GPIO18) = slide précédent
- Bouton RIGHT (GPIO0) = slide suivant
- Boucle (dernière → première)

### Launcher (slide 0)

Écran d'accueil avec liste des apps :
```
🤖 Nestor
📍 Radars
🌦 Météo
📈 Bourse

[⚙ Réglages]
```

Appui sur app → charge app (passe à son écran).
Appui long → options app (si dispo).

### Réglages (écran additionnel accessible depuis launcher)

```
WiFi :
  [Liste WiFi connus]
  [Connecter / Oublier]

Apps :
  ☑ Nestor
  ☑ Radars
  ☑ Météo
  ☑ Bourse

Nestor :
  ☑ Mode silence

Bourse :
  [Tickers configurés]
  [+] [−]

Météo :
  Ville : [Paris ▼]

Retour
```

Accès via bouton long power → menu (Veille / Arrêt / Annuler) ou via launcher → "Réglages".

---

## 14. Gestion de l'alimentation ESP32

### Bouton Power (AXP2101 GPIO13)

| Action | Résultat |
|---|---|
| Appui court (<1 s) | Écran ON/OFF (toggle) |
| Appui long (>1 s) | Menu Power modal |

### Menu Power

```
┌──────────────────┐
│  Alimentation    │
├──────────────────┤
│  💤 Veille       │
│  🔴 Arrêt        │
│  ❌ Annuler      │
└──────────────────┘
```

**Veille** :
- `esp_light_sleep_start()`
- RAM préservée, LVGL état intact
- Réveil sur appui bouton (~100 ms)
- Conso : 0,8–2 mA

**Arrêt** :
- Écran OFF
- `pmu.shutdown()` (AXP2101 coupe rails, conso quasi-nulle)
- Réveil bouton power
- Equivalent "off" complet

### Batterie — Affichage

Status bar : `[Battery 75%]`
- >30% : `#44CC44` vert
- 15–30% : `#F4A236` orange
- <15% : `#F44336` rouge
- <5% : clignotement rouge + alerte "Batterie faible"

---

## 15. Connectivité ESP32 (WiFi, OTA, BLE)

### WiFi

**Pas de captive portal** (sécurité). Configuration par :
1. BLE pairing + PWA Compagnon (préféré)
2. Écran réglages tactile (fallback)

Stockage : NVS (SSID + PWD), liste de WiFi connus.

### OTA (Over-The-Air)

**Première mise à jour** : USB-C obligatoire
- Arduino IDE : brancher ESP32, Outils → Port USB → Téléverser

**Mises à jour suivantes** : WiFi
- Arduino IDE : Outils → Port → sélectionner `compagnon at 192.168.x.x`
- Téléverser comme d'habitude (~30 sec)

**Paramètres** :
- Hostname : `compagnon`
- Port : 3232
- Mot de passe : `nestor_ota` (modifiable dans secrets.h)

### BLE

**Appairage** :
- GATT services : sync agents, GPS partagé, config WiFi
- Protocole : SYNC_HELLO/SYNC_PLAN/SYNC_DATA

**Data sharing** :
- GPS téléphone → ESP32 (Radars, Météo)
- WiFi configuration → ESP32
- Agents updates ↔ bidirectionnel

---

## 16. Procédure Arduino IDE

### Prérequis

1. **Arduino IDE** (version 2.x recommandée)
2. **URL board manager** : https://espressif.github.io/arduino-esp32/package_esp32_index.json
3. **Bibliothèques** : installer via Sketch → Include Library → Manage Libraries
   - `LVGL` (Waveshare patched, v9.x)
   - `Arduino_GFX_Library`
   - `SensorLib` (lewisxhe) — pour TouchDrvCSTXXX
   - `XPowersLib` (0.3.3)
   - `WiFiManager` (tzapu)

### Installation Board ESP32-S3

1. Arduino IDE → Préférences → URLs supplémentaires du gestionnaire de cartes
2. Ajouter : `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. Outils → Board → Board manager → chercher "esp32"
4. Installer "esp32 by Espressif Systems" (version **3.3.8** ou plus récent)

### Configuration Board

```
Board           : ESP32S3 Dev Module
Upload Speed    : 921600 bps
CPU Frequency   : 240 MHz
Flash Size      : 16 MB
Flash Mode      : QIO
Flash Frequency : 80 MHz
PSRAM           : OPI PSRAM (OPI 80 MHz)
Partition Scheme: 16M Flash (3MB APP / 9.9MB FATFS)
USB Mode        : USB CDC On Boot: Enabled
USB CDC on Boot : Enabled
Core Debug Level: None
```

### Placement lv_conf.h

```
~/Documents/Arduino/libraries/lv_conf.h
```

Copier depuis `compagnon/src/config/lv_conf.h` ou télécharger depuis LVGL.

### Compilation et upload

**Première fois** (USB) :
```
Sketch → Upload (Ctrl+U)
```

**Après première fois** (OTA WiFi) :
```
Outils → Port → sélectionner "compagnon at 192.168.x.x"
Sketch → Upload
```

---

## 17. Roadmap

### ✅ V1 Complete
- [x] Compagnon PWA hub
- [x] 4 apps modulaires (Nestor, Radars, Météo, Bourse)
- [x] ESP32 autonome LVGL
- [x] BLE pairing + WiFi config
- [x] STT/TTS autonome
- [x] Sync bidirectionnelle agents
- [x] Status bar + carousel
- [x] Power menu veille/arrêt
- [x] OTA WiFi

### 🚀 V2 (roadmap)
- [ ] Gardener local ESP32 (anti-collision avec PWA gardener)
- [ ] Villes favorites Météo
- [ ] QR code portail WiFi ESP32 (si ajout caméra)
- [ ] Rotation auto écran (IMU QMI8658)
- [ ] Historique de conversation exportable
- [ ] Métriques enrichies (tokens, temps réponse)
- [ ] Support multi-langues (EN, ES)
- [ ] Backend Ollama local

### 📋 V3+ (distant)
- [ ] Autre appareil Compagnon (montre, hub)
- [ ] Sync multi-devices (PWA + 2 ESP32)
- [ ] Intégrations calendario / email
- [ ] Mode autonome complet (aucun cloud si Ollama local)

---

**FIN SPEC.md — Verrouillée V3. Prête pour restructure repo + code.**
