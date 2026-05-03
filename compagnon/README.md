# Nestor — Compagnon ESP32

Code source du compagnon Nestor pour la carte **Waveshare ESP32-S3 Touch AMOLED 2.16"**.

## Matériel cible
- MCU : ESP32-S3
- Écran : AMOLED 2.16" tactile 480×480 px (SH8601 / CO5300)
- Batterie : 1000 mAh 3.7V AT103030
- Framework : Arduino IDE 2.x + LVGL

## Architecture : Super-App monolithique

Tout le code est dans **un seul projet Arduino** : `NestorOS/`.
Pas de reboot entre les apps — changement d'écran LVGL instantané.

```
compagnon/
└── NestorOS/              ← Ouvrir NestorOS.ino dans Arduino IDE
    ├── NestorOS.ino        ← setup() + loop() uniquement
    ├── display_init        ← Init AMOLED + touch + LVGL
    ├── bootloader_ui       ← Menu graphique de sélection
    ├── orchestrator        ← Chef d'orchestre (SYNC ↔ PWA)
    ├── brain               ← Cerveau IA (SYNC ↔ PWA)
    ├── nestor_app          ← App Nestor compagnon
    ├── radar_app           ← App Radar (placeholder)
    ├── lora_app            ← App LoRa/GPS (placeholder)
    └── histoire_app        ← App Histoire (placeholder)
```

## ⚠️ Règle de synchronisation

`brain.h/.cpp` et `orchestrator.h/.cpp` sont le **cerveau partagé** entre la PWA et l'ESP32.  
Toute modification dans ces fichiers **doit être répercutée** dans `src/brain/` et `src/orchestrator/` de la PWA, et vice-versa.

## Librairies Arduino requises

| Lib | Source | Installation |
|-----|--------|--------------|
| `Mylibrary` | Repo Waveshare (driver AMOLED) | Copier dans `Documents/Arduino/libraries/` |
| `lvgl` | Repo Waveshare (version testée) | Copier dans `Documents/Arduino/libraries/` |
| `lv_conf.h` | Repo Waveshare | Copier directement dans `Documents/Arduino/libraries/` |
| `XPowersLib` | Library Manager Arduino | Mise à jour auto OK |
| `SensorLib` | Library Manager Arduino | Mise à jour auto OK |
