# SmartHome Integration — Sans serveur local

Cette intégration contrôle tes appareils **uniquement via les clouds existants** (Tuya + SinricPro + Ecovacs), sans Home Assistant ni autre serveur local.

## Architecture

```
ESP32 ──HTTPS──► Tuya Cloud API   → lumières Zigbee/WiFi, capteurs temp/humidité
ESP32 ──WSS──►  SinricPro Cloud   → prises connectées Alexa
ESP32 ──HTTPS──► Ecovacs Cloud    → aspirateur X8 Pro Omni
```

## Configuration requise dans `config.h`

```cpp
// Tuya Cloud (https://iot.tuya.com → ton projet)
#define TUYA_CLIENT_ID      "xxxxxxxxxxxxxxxxxxxxxxxx"
#define TUYA_CLIENT_SECRET  "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
#define TUYA_SENSOR_ID      "device_id_de_ton_capteur"
#define TUYA_LIGHT_ID       "device_id_de_ta_lumiere"

// SinricPro (https://sinric.pro → ton app)
#define SINRIC_APP_KEY      "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define SINRIC_APP_SECRET   "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
#define SINRIC_PLUG_ID      "device_id_sinric_de_ta_prise"

// Ecovacs
#define ECOVACS_EMAIL          "ton@email.com"
#define ECOVACS_PASSWORD_HASH  "md5_lowercase_de_ton_mot_de_passe"
// md5 sur Linux: echo -n 'monMotDePasse' | md5sum
```

## Trouver les Device IDs Tuya

1. Aller sur https://iot.tuya.com → ton projet
2. `Device Management` → liste de tes appareils avec leurs IDs

## Intégration dans main.cpp

```cpp
#include "src/api/tuya_api.h"
#include "src/api/sinricpro_bridge.h"
#include "src/ui/smarthome_app.h"
#include "src/ui/ecovacs_app.h"

TuyaAPI tuya(TUYA_CLIENT_ID, TUYA_CLIENT_SECRET);
SinricProBridge sinric(SINRIC_APP_KEY, SINRIC_APP_SECRET);
SmartHomeApp smartHomeApp(&tuya, &sinric);
EcovacsApp ecovacsApp;

void setup() {
    // ... WiFi connect ...
    tuya.refreshToken();
    sinric.begin();
    // ... register dans ton app manager ...
}

void loop() {
    sinric.handle();  // WebSocket SinricPro
    // refresh toutes les 30s
    static unsigned long lastRefresh = 0;
    if (millis() - lastRefresh > 30000) {
        lastRefresh = millis();
        if (!tuya.isTokenValid()) tuya.refreshToken();
        smartHomeApp.update();
        ecovacsApp.update();
    }
}
```

## Libs Arduino nécessaires

- `ArduinoJson` >= 7.x
- `SinricPro` par Boris Jaeger (library manager)
- `HTTPClient` (inclus ESP32 Arduino core)
