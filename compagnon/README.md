# ESP32 Compagnon — Instructions Arduino IDE

**Procédure complète pour compiler et uploader le firmware Compagnon sur ESP32-S3 Waveshare AMOLED 2.16"**

---

## ⚙️ Prérequis

- **Arduino IDE** version 2.x
- **Câble USB-C** (pour première upload)
- **ESP32-S3 Waveshare AMOLED 2.16"**

---

## 1️⃣ Installation du Board ESP32-S3

### URL du gestionnaire de cartes

Arduino IDE → Préférences → URLs supplémentaires du gestionnaire de cartes

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

### Installer

Outils → Board → Board manager → chercher "esp32" → installer "esp32 by Espressif Systems" (v3.3.8+)

---

## 2️⃣ Installation des bibliothèques

Outils → Gérer les bibliothèques → chercher et installer :

- **LVGL** (9.x)
- **Arduino_GFX_Library**
- **SensorLib** (lewisxhe)
- **XPowersLib** (0.3.3)
- **WiFiManager** (tzapu)

---

## 3️⃣ Copier lv_conf.h

⚠️ **CRITIQUE** : LVGL a besoin de `lv_conf.h` à la racine `~/Documents/Arduino/libraries/`

```bash
cp compagnon/src/config/lv_conf.h ~/Documents/Arduino/libraries/lv_conf.h
```

---

## 4️⃣ Configuration du Board

**Outils** → Sélectionner :

```
Board                : ESP32S3 Dev Module
Port                 : /dev/cu.usbserial-...  (déterminé après branchement)
Upload Speed         : 921600
CPU Frequency        : 240 MHz
Flash Size           : 16 MB
PSRAM                : OPI PSRAM (OPI 80 MHz)
Partition Scheme     : 16M Flash (3MB APP / 9.9MB FATFS)
USB CDC On Boot      : Enabled
Core Debug Level     : None
```

---

## 5️⃣ Copier et remplir secrets.h

```bash
cp compagnon/src/config/secrets.template.h compagnon/src/config/secrets.h
```

Éditer `compagnon/src/config/secrets.h` et remplir tes clés API (Groq, Gemini, Météo, Twelve Data, etc.)

⚠️ **Ne jamais commiter secrets.h**

---

## 6️⃣ Première compilation et upload (USB)

1. Brancher ESP32 en USB-C
2. Arduino IDE → Outils → Port → sélectionner le port `/dev/cu.usb*`
3. **Sketch → Téléverser** (Ctrl+U)
4. Attendre ~30 sec → "Téléversement terminé."

---

## 7️⃣ Updates suivants (OTA WiFi)

Après la première upload :

1. **Outils → Port** → tu vois `compagnon at 192.168.x.x (pool)` ✨
2. Sélectionner ce port
3. **Sketch → Téléverser** (Ctrl+U)
4. Attendre ~30 sec → rechargement WiFi automatique ✅

Pas besoin de câble USB pour les updates suivants !

---

## 🔧 Troubleshooting

- **Écran blanc** : vérifier `lv_conf.h` copié, vérifier pin_config.h, moniteur série pour logs
- **Port USB n'apparaît pas** : essayer autre câble, redémarrer Arduino IDE
- **secrets.h not found** : vérifier fichier copié (pas juste .template.h)
- **OTA n'apparaît pas** : vérifier même WiFi, attendre 10 sec après boot

---

**État** : ✅ Ready. Start with step 1 → 7. Bon code ! 🚀
