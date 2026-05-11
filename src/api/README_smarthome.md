# SmartHome Integration — Sans serveur local

Contrôle toute ta domotique SmartLife/Tuya + aspirateur Ecovacs X8 Pro Omni
directement depuis l'ESP32, sans Home Assistant ni serveur local.

## Architecture

```
ESP32 ──HTTPS──► Tuya Cloud API      → TOUS les devices SmartLife (lumières, prises, capteurs, volets...)
ESP32 ──HTTPS──► Ecovacs Open API    → X8 Pro Omni (nettoyage, retour base, état, base station)
```

## 1. Configuration Tuya

### Créer le projet Cloud
1. Aller sur https://iot.tuya.com → Cloud → Create Cloud Project
2. Région : **Europe Occidentale**, Type : **Smart Home**
3. Récupérer **Client ID** + **Client Secret**
4. Dans le projet → onglet **"Link Tuya App Account"** → scanner le QR avec l'app SmartLife
   ⇒ Tous tes devices SmartLife sont maintenant accessibles via l'API

### Trouver les device IDs
- Pas besoin de les connaître à l'avance ! `TuyaAPI::listDevices()` les récupère dynamiquement.
- Pour les hard-coder en config.h : iot.tuya.com → Device Management → Device List

```cpp
// config.h
#define TUYA_CLIENT_ID      "xxxxxxxxxxxxxxxxxxxxxxxx"
#define TUYA_CLIENT_SECRET  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
// IDs optionnels (sinon listDevices() les découvre)
#define TUYA_SENSOR_ID      "..."
#define TUYA_LIGHT_ID       "..."
#define TUYA_PLUG_ID        "..."
```

## 2. Configuration Ecovacs

### Obtenir l'Access Key
1. Aller sur https://open.ecovacs.com
2. Cliquer **"github获取AK"** → login GitHub OAuth
3. Un **Access Key** est généré automatiquement
4. Utiliser le **nickname** de ton robot tel qu'il apparaît dans l'app Ecovacs

```cpp
// config.h
#define ECOVACS_ACCESS_KEY   "votre_access_key"
#define ECOVACS_ROBOT_NAME   "X8 Pro"  // nickname dans l'app Ecovacs
```

## 3. Intégration dans main.cpp

```cpp
#include "src/api/tuya_api.h"
#include "src/api/ecovacs_api.h"
#include "src/ui/smarthome_app.h"
#include "src/ui/ecovacs_app.h"

TuyaAPI      tuya(TUYA_CLIENT_ID, TUYA_CLIENT_SECRET);
EcovacsAPI   ecovacs(ECOVACS_ACCESS_KEY);
SmartHomeApp smartHomeApp(&tuya);
EcovacsApp   ecovacsApp(&ecovacs, ECOVACS_ROBOT_NAME);

void setup() {
    // ... WiFi connect ...
    tuya.refreshToken();
    // Découverte automatique de tous les devices
    String uid;
    if (tuya.getCurrentUserUid(uid)) {
        std::vector<TuyaDevice> devices;
        tuya.listDevices(uid, devices);
        for (auto& d : devices) {
            Serial.printf("Device: %s [%s] online=%d\n",
                d.name.c_str(), d.category.c_str(), d.online);
        }
    }
}

void loop() {
    static unsigned long last = 0;
    if (millis() - last > 30000) {
        last = millis();
        if (!tuya.isTokenValid()) tuya.refreshToken();
        smartHomeApp.update();
        ecovacsApp.update();
    }
}
```

## 4. Libs Arduino nécessaires

| Lib | Source |
|---|---|
| `ArduinoJson` >= 7.x | Library Manager |
| `HTTPClient` | inclus ESP32 Arduino core |
| `WiFiClientSecure` | inclus ESP32 Arduino core |
