# Nestor — Système dual PWA + ESP32

**Version v3** — Architecture duale : PWA Compagnon (hub) + 4 apps autonomes (Nestor, Radars, Météo, Bourse) + ESP32 autonome avec sync bidirectionnelle.

---

## 📋 Structure du projet

```
Nestor/
├── SPEC.md                          # Spécification technique complète V3
├── README.md                        # Ce fichier
├── .gitignore
├── secrets.template.js              # Template clés API PWA
├── index.html, manifest.json        # Point d'entrée PWA
├── service-worker.js                # Service Worker offline
├── css/
│   └── style.css
└── src/                             # PWA COMPAGNON HUB
    ├── app.js                       # Bootstrap + state machine
    ├── api/
    │   ├── backends.js              # LLM routing (Groq, OpenRouter, etc.)
    │   ├── search.js                # Web search (Serper, DDG)
    │   └── tts.js                   # Text-to-Speech (browser, Gemini)
    ├── core/
    │   ├── orchestrator-engine.js   # Méta-planificateur
    │   ├── default-agents.js        # Agents par défaut
    │   └── gardener.js              # Maintenance agents
    ├── storage/
    │   └── agents-db.js             # IndexedDB + localStorage
    ├── sync/
    │   └── ble-sync.js              # BLE sync protocol
    ├── ui/
    │   ├── compagnon-hub.js         # Hub de gestion central
    │   ├── settings.js              # Réglages généraux + clés API
    │   ├── ble-pairing.js           # Modal appairage BLE
    │   └── apps/
    │       ├── nestor-app.js        # Chat avec agents
    │       ├── radars-app.js        # GPS + alertes radars
    │       ├── meteo-app.js         # Prévisions météo
    │       └── bourse-app.js        # Cours boursiers
    ├── device/
    │   ├── ble-manager.js           # Web Bluetooth API
    │   └── provisioning.js          # Configuration WiFi BLE
    └── bt/
        ├── ble.js                   # Services GATT
        └── ble_protocol.js          # Protocoles BLE

compagnon/                           # ESP32 COMPAGNON AUTONOME
├── compagnon.ino                    # Entry point sketch
├── README.md                        # Instructions Arduino IDE
├── src/config/
│   ├── pin_config.h                 # Pins officielles Waveshare
│   ├── lv_conf.h                    # Config LVGL
│   └── secrets.template.h           # Template clés API
├── src/apps/
│   ├── nestor/
│   │   ├── nestor_app.h/cpp         # Chat LVGL, STT/TTS
│   │   └── stt_engine.h/cpp         # STT local ES7210
│   ├── radars/
│   │   ├── radar_app.h/cpp          # GPS + alertes radar
│   │   └── lufop_api.h/cpp          # API Lufop
│   ├── meteo/
│   │   ├── meteo_app.h/cpp          # Prévisions
│   │   └── geoloc_provider.h/cpp    # GPS BLE fallback
│   ├── bourse/
│   │   ├── bourse_app.h/cpp         # Cours boursiers
│   │   └── twelve_data_api.h/cpp    # API Twelve Data
│   └── app_base.h
├── src/core/
│   ├── orchestrator.h/cpp
│   ├── brain.h/cpp
│   └── agents_registry.h/cpp
├── src/system/
│   ├── wifi_mgr.h/cpp              # WiFi + captif sécurisé
│   └── ...
├── src/ui/
│   ├── launcher.h/cpp              # Carousel LVGL
│   ├── status_bar.h/cpp            # Barre statut (WiFi, BLE, batterie)
│   └── power_menu.h/cpp            # Menu veille/arrêt
├── src/hal/
│   ├── display.h/cpp               # AMOLED CO5300
│   ├── touch.h/cpp                 # CST9220 tactile
│   ├── pmu.h/cpp                   # AXP2101 alimentation
│   ├── imu.h/cpp                   # QMI8658 orientation
│   └── audio.h/cpp                 # ES8311 + ES7210
├── src/net/
│   ├── ota.h/cpp                   # OTA WiFi
│   └── ble.h/cpp                   # BLE GATT services
└── src/storage/
    ├── littlefs.h/cpp              # LittleFS agents JSON
    └── nvs_mgr.h/cpp               # NVS preferences
```

---

## 🚀 Démarrage rapide

### PWA Compagnon (Navigateur)

1. Cloner le repo
   ```bash
   git clone https://github.com/sicho95/Nestor.git
   cd Nestor
   ```

2. Installer dépendances (si Node requis)
   ```bash
   npm install
   ```

3. Copier secrets.template.js → secrets.js et remplir clés API
   ```bash
   cp secrets.template.js secrets.js
   # Éditer secrets.js avec tes clés
   ```

4. Servir la PWA (simple HTTP)
   ```bash
   python3 -m http.server 8000
   # Puis accéder http://localhost:8000
   ```

### ESP32 Compagnon (Arduino IDE)

Voir [compagnon/README.md](compagnon/README.md) pour procédure détaillée.

**TL;DR** :
1. Arduino IDE 2.x + board ESP32-S3 (v3.3.8+)
2. Installer bibliothèques (LVGL, Arduino_GFX, SensorLib, XPowersLib, WiFiManager)
3. Copier `compagnon/src/config/lv_conf.h` → `~/Documents/Arduino/libraries/lv_conf.h`
4. Copier `secrets.template.h` → `secrets.h`, remplir clés
5. Brancher ESP32 USB-C
6. Sketch → Upload
7. Après première upload : OTA WiFi via Arduino IDE

---

## 🔗 Synchronisation PWA ↔ ESP32

- **BLE Pairing** : code 6 chiffres aléatoire (sécurisé)
- **WiFi Config** : PWA → liste WiFi du téléphone → ESP32 (NVS)
- **Sync Agents** : last-write-wins par `updatedAt`, triggers : lancement Nestor, création/modif agent, 2h polling
- **Clés API** : stockées en localStorage PWA + NVS chiffré ESP32, synchronisées au pairing

---

## 📱 4 Apps V1 — Dual PWA + ESP32

### 1. Nestor — Assistant IA
- **PWA** : chat clavier + Web Speech STT, TTS browser/Gemini
- **ESP32** : chat LVGL + ES7210 STT + ES8311 TTS (autonome)
- **Agents** : partagés, sync bidirectionnelle, améliorés par Jardinier

### 2. Radars — GPS + Alertes
- **PWA** : carte Leaflet, position GPS tel, Lufop + Blitzer APIs
- **ESP32** : liste LVGL, GPS via BLE tel, APIs directs (autonome si WiFi)

### 3. Météo — Prévisions
- **PWA** : api.meteo-concept.com, géoloc GPS tel, prévisions J+3
- **ESP32** : prévisions LVGL, géoloc GPS BLE (fallback dernière pos / Paris)

### 4. Bourse — Cours en temps réel
- **PWA** : Twelve Data (4 tickers 9h–18h, refresh 1.5 min), Finnhub option
- **ESP32** : tableau LVGL, Twelve Data direct (autonome si WiFi)

---

## 🔐 Sécurité

- **Secrets** : `secrets.js` (PWA) + `secrets.h` (ESP32) gitignorés
- **BLE** : code 6 chiffres, appairage standard OS
- **WiFi** : pas de captive portal malveillant (zéro endpoint /update ou /erase)
- **API keys** : stockées en localStorage (PWA) + NVS chiffré (ESP32), jamais exposées en URL

---

## 📚 Documentation complète

Voir **[SPEC.md](SPEC.md)** pour :
- Architecture détaillée
- Protocoles BLE GATT
- STT/TTS autonome ESP32
- Sync bidirectionnelle agents
- Procédure Arduino IDE pas-à-pas
- Roadmap V2+

---

## 👤 Auteur

Damien (sicho95) — mai 2026

---

## 📝 Licence

MIT (ou tu précises)

---

**État** : ✅ V3 verrouillée, prête pour code.