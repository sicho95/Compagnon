# Nestor — Compagnon ESP32

Code source du compagnon Nestor pour la carte **Waveshare ESP32-S3 Touch AMOLED 2.16"**.

## Matériel cible
- MCU : ESP32-S3
- Écran : AMOLED 2.16" tactile (SH8601 / CO5300)
- Batterie : 1000 mAh 3.7V AT103030
- Framework : Arduino IDE + bibliothèques LVGL

## Structure
```
compagnon/
├── bootloader/         # Bootloader graphique LVGL (sélection d'app)
├── nestor_app/         # Application Nestor mode compagnon
├── shared/             # Logique partagée (cerveau/orchestrateur)
│   ├── brain/          # Moteur de raisonnement — SYNC avec PWA/src/brain/
│   └── orchestrator/   # Orchestrateur tâches — SYNC avec PWA/src/orchestrator/
├── future_apps/        # Placeholder apps futures (Radar, LoRa, GPS, Histoire)
└── README.md
```

## ⚠️ Règle de synchronisation
Tout changement dans `shared/brain/` ou `shared/orchestrator/` **doit** être répercuté
dans le dossier miroir `../src/brain/` et `../src/orchestrator/` de la PWA, et vice-versa.

## IDE recommandé
Arduino IDE 2.x avec :
- Board package : `esp32` by Espressif (via Board Manager)
- Bibliothèque : LVGL 9.x
- Bibliothèque : TFT_eSPI ou driver Waveshare natif
