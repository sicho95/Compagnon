/*
 * Nestor Compagnon — ESP32-S3 Waveshare AMOLED 2.16"
 * Fichier principal Arduino IDE
 *
 * Ordre d'initialisation critique :
 *   1. PMU  — active les rails ALDO1/ALDO3 et configure le bouton power
 *   2. NVS  — crée le namespace "compagnon" si absent (flash vierge)
 *   3. Affichage — reset matériel pin 2 (partagé avec touch)
 *   4. Touch — attend 500 ms post-reset, puis init CST9220
 *   5. IMU  — QMI8658 (orientation)
 *   6. UI   — status bar (lv_layer_top), puis launcher
 *   7. Réseau — WiFiManager + OTA
 *   8. Orchestrateur — démarre le cerveau/planificateur
 *   9. Callback power — relie appui long PMU → menu UI
 */

#include "src/hal/display.h"
#include "src/hal/touch.h"
#include "src/hal/imu.h"
#include "src/hal/pmu.h"
#include "src/system/orchestrator.h"
#include "src/system/wifi_mgr.h"
#include "src/net/ota.h"
#include "src/net/ble_mgr.h"
#include "src/config/nvs_config.h"
#include "src/ui/status_bar.h"
#include "src/ui/launcher.h"
#include <ArduinoJson.h>

// ─── Mapping noms longs PWA → clés NVS courtes (≤ 15 chars) ──────────────────
struct KeyMapping { const char *pwa_name; const char *nvs_name; };
static const KeyMapping KEY_MAP[] = {
    { "GROQ_API_KEY",           NVS_KEY_GROQ         },
    { "GEMINI_API_KEY",         NVS_KEY_GEMINI       },
    { "SERPER_API_KEY",         NVS_KEY_SERPER       },
    { "OPENROUTER_API_KEY",     NVS_KEY_OPENROUTER   },
    { "TWELVE_DATA_API_KEY",    NVS_KEY_TWELVEDATA   },
    { "TWELVEDATA_API_KEY",     NVS_KEY_TWELVEDATA   },
    { "METEO_CONCEPT_API_KEY",  NVS_KEY_METEO        },
    { "METEO_API_KEY",          NVS_KEY_METEO        },
    { "SPOTIFY_CLIENT_ID",      NVS_KEY_SPOTIFY_ID   },
    { "SPOTIFY_CLIENT_SECRET",  NVS_KEY_SPOTIFY_SEC  },
    // Tuya domotique
    { "TUYA_CLIENT_ID",         NVS_KEY_TUYA_ID      },
    { "TUYA_CLIENT_SECRET",     NVS_KEY_TUYA_SEC     },
    { NVS_KEY_TUYA_ID,          NVS_KEY_TUYA_ID      },
    { NVS_KEY_TUYA_SEC,         NVS_KEY_TUYA_SEC     },
    // Ecovacs robot
    { "ECOVACS_EMAIL",          NVS_KEY_ECOVACS_U    },
    { "ECOVACS_PASSWORD",       NVS_KEY_ECOVACS_P    },
    { NVS_KEY_ECOVACS_U,        NVS_KEY_ECOVACS_U    },
    { NVS_KEY_ECOVACS_P,        NVS_KEY_ECOVACS_P    },
    // Clés courtes (pass-through)
    { NVS_KEY_GROQ,             NVS_KEY_GROQ         },
    { NVS_KEY_GEMINI,           NVS_KEY_GEMINI       },
    { NVS_KEY_SERPER,           NVS_KEY_SERPER       },
    { NVS_KEY_OPENROUTER,       NVS_KEY_OPENROUTER   },
    { NVS_KEY_TWELVEDATA,       NVS_KEY_TWELVEDATA   },
    { NVS_KEY_METEO,            NVS_KEY_METEO        },
    { NVS_KEY_SPOTIFY_ID,       NVS_KEY_SPOTIFY_ID   },
    { NVS_KEY_SPOTIFY_SEC,      NVS_KEY_SPOTIFY_SEC  },
    { nullptr, nullptr }
};

static const char *resolve_nvs_key(const char *pwa_key) {
    if (!pwa_key || !pwa_key[0]) return nullptr;
    for (int i = 0; KEY_MAP[i].pwa_name != nullptr; i++) {
        if (strcasecmp(KEY_MAP[i].pwa_name, pwa_key) == 0)
            return KEY_MAP[i].nvs_name;
    }
    return nullptr;
}

// ─── Pont BLE → WiFi provisioning ────────────────────────────────────────────
static void ble_wifi_prov_cb(const char *json) {
    if (!json) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) { Serial.print("[BLE/WIFI] JSON invalide: "); Serial.println(err.c_str()); return; }
    const char *ssid = doc["s"] | doc["ssid"] | "";
    const char *pwd  = doc["p"] | doc["pwd"]  | "";
    if (!ssid || !ssid[0]) { Serial.println("[BLE/WIFI] SSID vide — ignore"); return; }
    Serial.printf("[BLE/WIFI] Provision %s\n", ssid);
    wifi_mgr_provision(ssid, pwd);
}

// ─── Pont BLE → Agent Sync ──────────────────────────────────────────────────
static void ble_agent_sync_cb(const char *json) {
    if (!json) return;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) { Serial.printf("[BLE/AGENT] JSON invalide: %s\n", err.c_str()); return; }

    const char *cmd = doc["cmd"] | "";

    // Commande vide : ignorer silencieusement
    if (!cmd || cmd[0] == '\0') return;

    if (strcmp(cmd, "set_api_key") == 0) {
        const char *pwa_key = doc["key"] | "";
        const char *val     = doc["val"] | doc["value"] | "";
        if (!pwa_key[0]) { Serial.println("[BLE/AGENT] set_api_key: champ 'key' manquant"); return; }
        if (!val[0]) { Serial.printf("[BLE/AGENT] set_api_key: valeur vide pour %s\n", pwa_key); return; }
        const char *nvs_key = resolve_nvs_key(pwa_key);
        if (!nvs_key) {
            char ack[160];
            snprintf(ack, sizeof(ack), "{\"cmd\":\"set_api_key_ack\",\"key\":\"%s\",\"ok\":false,\"err\":\"unknown_key\"}", pwa_key);
            ble_mgr_notify_agent_sync(ack);
            return;
        }
        bool ok = nvs_set_api_key(nvs_key, val);
        char ack[160];
        snprintf(ack, sizeof(ack), "{\"cmd\":\"set_api_key_ack\",\"key\":\"%s\",\"ok\":%s}", pwa_key, ok ? "true" : "false");
        ble_mgr_notify_agent_sync(ack);
        return;
    }

    if (strcmp(cmd, "get_api_keys") == 0) {
        char json_out[512];
        nvs_list_api_keys_json(json_out, sizeof(json_out));
        char resp[600];
        snprintf(resp, sizeof(resp), "{\"cmd\":\"api_keys_status\",\"keys\":%s}", json_out);
        ble_mgr_notify_agent_sync(resp);
        return;
    }

    if (strcmp(cmd, "clear_api_key") == 0) {
        const char *pwa_key = doc["key"] | "";
        if (!pwa_key[0]) return;
        const char *nvs_key = resolve_nvs_key(pwa_key);
        if (!nvs_key) return;
        nvs_clear_api_key(nvs_key);
        char ack[160];
        snprintf(ack, sizeof(ack), "{\"cmd\":\"clear_api_key_ack\",\"key\":\"%s\",\"ok\":true}", pwa_key);
        ble_mgr_notify_agent_sync(ack);
        return;
    }

    // ─── battery_status / get_device_status ──────────────────────────────────
    if (strcmp(cmd, "battery_status") == 0 || strcmp(cmd, "get_device_status") == 0) {
        int  bat_pct  = hal_pmu_battery_pct();   // 0-100, -1 si PMIC indisponible
        bool charging = (bat_pct >= 0) && WiFi.isConnected(); // pas d'API isCharging exposée
        char resp[160];
        snprintf(resp, sizeof(resp),
            "{\"cmd\":\"device_status\",\"battery\":%d,\"charging\":%s,\"wifi\":%s}",
            (bat_pct >= 0) ? bat_pct : 0,
            charging       ? "true"  : "false",
            WiFi.isConnected() ? "true" : "false"
        );
        ble_mgr_notify_agent_sync(resp);
        Serial.printf("[BLE/AGENT] battery_status -> bat=%d%%\n", bat_pct);
        return;
    }

    Serial.printf("[BLE/AGENT] cmd inconnue: %s\n", cmd);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] Nestor Compagnon — demarrage");

    hal_pmu_init();
    nvs_config_init();
    hal_display_init();
    hal_touch_init();
    hal_imu_init();

    ui_status_bar_init();
    wifi_mgr_init();
    net_ota_init();
    ble_mgr_init();
    ble_mgr_set_wifi_prov_cb(ble_wifi_prov_cb);
    ble_mgr_set_agent_sync_cb(ble_agent_sync_cb);

    orchestrator_init();
    ui_launcher_init();

    hal_pmu_set_long_press_cb(ui_power_menu_show);

    Serial.println("[BOOT] Pret.");
}

void loop() {
    lv_timer_handler();

    hal_pmu_tick();
    wifi_mgr_tick();
    net_ota_tick();
    ble_mgr_tick();

    ui_status_bar_tick();
    hal_imu_tick();
    if (hal_imu_changed()) {
        static const lv_display_rotation_t rot_map[] = {
            LV_DISPLAY_ROTATION_270,
            LV_DISPLAY_ROTATION_0,
            LV_DISPLAY_ROTATION_90,
            LV_DISPLAY_ROTATION_180,
        };
        lv_display_set_rotation(hal_display_get(), rot_map[hal_imu_orientation()]);
    }

    orchestrator_tick();
    ui_launcher_btn_tick();

    delay(1);
}
