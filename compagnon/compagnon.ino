/*
 * Nestor Compagnon — ESP32-S3 Waveshare AMOLED 2.16"
 * Fichier principal Arduino IDE
 *
 * Ordre d'initialisation critique :
 *   1. PMU  — active les rails ALDO1/ALDO3 et configure le bouton power
 *   2. Affichage — reset matériel pin 2 (partagé avec touch)
 *   3. Touch — attend 500 ms post-reset, puis init CST9220
 *   4. IMU  — QMI8658 (orientation)
 *   5. UI   — status bar (lv_layer_top), puis launcher
 *   6. Réseau — WiFiManager + OTA
 *   7. Orchestrateur — démarre le cerveau/planificateur
 *   8. Callback power — relie appui long PMU → menu UI
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
// La PWA envoie des noms longs (ex: "METEO_CONCEPT_API_KEY") mais la NVS ESP32
// impose une limite stricte de 15 caractères. Ce mapping fait le pont.
struct KeyMapping { const char *pwa_name; const char *nvs_name; };
static const KeyMapping KEY_MAP[] = {
    { "GROQ_API_KEY",           NVS_KEY_GROQ         },
    { "GEMINI_API_KEY",         NVS_KEY_GEMINI       },
    { "SERPER_API_KEY",         NVS_KEY_SERPER       },
    { "OPENROUTER_API_KEY",     NVS_KEY_OPENROUTER   },
    { "TWELVE_DATA_API_KEY",    NVS_KEY_TWELVEDATA   },
    { "TWELVEDATA_API_KEY",     NVS_KEY_TWELVEDATA   },  // variante PWA
    { "METEO_CONCEPT_API_KEY",  NVS_KEY_METEO        },
    { "METEO_API_KEY",          NVS_KEY_METEO        },  // variante courte
    { "SPOTIFY_CLIENT_ID",      NVS_KEY_SPOTIFY_ID   },
    { "SPOTIFY_CLIENT_SECRET",  NVS_KEY_SPOTIFY_SEC  },
    // Clés courtes déjà valides (passthrough)
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

// Retourne la clé NVS courte correspondant au nom PWA, ou nullptr si inconnu.
static const char *resolve_nvs_key(const char *pwa_key) {
    if (!pwa_key || !pwa_key[0]) return nullptr;
    for (int i = 0; KEY_MAP[i].pwa_name != nullptr; i++) {
        if (strcasecmp(KEY_MAP[i].pwa_name, pwa_key) == 0)
            return KEY_MAP[i].nvs_name;
    }
    return nullptr;
}

// ─── Pont BLE → WiFi provisioning ────────────────────────────────────────────
// La PWA envoie un JSON {"s":"SSID","p":"password"} via la caractéristique
// WIFI_PROV du service BLE. Ce callback parse le JSON et appelle
// wifi_mgr_provision() qui enregistre en NVS et reconnecte.
static void ble_wifi_prov_cb(const char *json) {
    if (!json) return;
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.print("[BLE/WIFI] JSON invalide: ");
        Serial.println(err.c_str());
        return;
    }
    const char *ssid = doc["s"] | doc["ssid"] | "";
    const char *pwd  = doc["p"] | doc["pwd"]  | "";
    if (!ssid || !ssid[0]) {
        Serial.println("[BLE/WIFI] SSID vide — ignore");
        return;
    }
    Serial.printf("[BLE/WIFI] Provision %s (len=%u)\n", ssid, (unsigned)strlen(pwd));
    wifi_mgr_provision(ssid, pwd);
}

// ─── Pont BLE → Agent Sync (dispatch commandes PWA) ──────────────────────────
// La PWA envoie des commandes JSON via CHAR_AGENT_SYNC.
// Commandes supportées :
//   {"cmd":"set_api_key", "key":"METEO_CONCEPT_API_KEY", "val":"xxxxx"}
//   {"cmd":"get_api_keys"}  → répond via ble_mgr_notify_agent_sync()
//   {"cmd":"clear_api_key","key":"..."}
static void ble_agent_sync_cb(const char *json) {
    if (!json) return;
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[BLE/AGENT] JSON invalide: %s\n", err.c_str());
        return;
    }

    const char *cmd = doc["cmd"] | "";

    // ── set_api_key ──────────────────────────────────────────────────────────
    if (strcmp(cmd, "set_api_key") == 0) {
        const char *pwa_key = doc["key"] | "";
        const char *val     = doc["val"] | doc["value"] | "";

        if (!pwa_key[0]) {
            Serial.println("[BLE/AGENT] set_api_key: champ 'key' manquant");
            return;
        }
        if (!val[0]) {
            Serial.printf("[BLE/AGENT] set_api_key: valeur vide pour %s\n", pwa_key);
            return;
        }

        // Résoudre le nom long PWA → clé NVS courte (≤ 15 chars)
        const char *nvs_key = resolve_nvs_key(pwa_key);
        if (!nvs_key) {
            Serial.printf("[BLE/AGENT] set_api_key: clé PWA inconnue: '%s'\n", pwa_key);
            char ack[160];
            snprintf(ack, sizeof(ack),
                     "{\"cmd\":\"set_api_key_ack\",\"key\":\"%s\",\"ok\":false,\"err\":\"unknown_key\"}",
                     pwa_key);
            ble_mgr_notify_agent_sync(ack);
            return;
        }

        bool ok = nvs_set_api_key(nvs_key, val);
        Serial.printf("[BLE/AGENT] set_api_key '%s' → NVS '%s' : %s\n",
                      pwa_key, nvs_key, ok ? "OK" : "ERREUR");

        // Notifier la PWA du résultat (on renvoie le nom PWA pour le matching côté JS)
        char ack[160];
        snprintf(ack, sizeof(ack),
                 "{\"cmd\":\"set_api_key_ack\",\"key\":\"%s\",\"ok\":%s}",
                 pwa_key, ok ? "true" : "false");
        ble_mgr_notify_agent_sync(ack);
        return;
    }

    // ── get_api_keys ─────────────────────────────────────────────────────────
    if (strcmp(cmd, "get_api_keys") == 0) {
        char json_out[512];
        nvs_list_api_keys_json(json_out, sizeof(json_out));
        char resp[600];
        snprintf(resp, sizeof(resp), "{\"cmd\":\"api_keys_status\",\"keys\":%s}", json_out);
        ble_mgr_notify_agent_sync(resp);
        Serial.println("[BLE/AGENT] get_api_keys envoyé");
        return;
    }

    // ── clear_api_key ────────────────────────────────────────────────────────
    if (strcmp(cmd, "clear_api_key") == 0) {
        const char *pwa_key = doc["key"] | "";
        if (!pwa_key[0]) return;
        const char *nvs_key = resolve_nvs_key(pwa_key);
        if (!nvs_key) {
            Serial.printf("[BLE/AGENT] clear_api_key: clé inconnue '%s'\n", pwa_key);
            return;
        }
        nvs_clear_api_key(nvs_key);
        Serial.printf("[BLE/AGENT] clear_api_key '%s' → NVS '%s'\n", pwa_key, nvs_key);
        char ack[160];
        snprintf(ack, sizeof(ack),
                 "{\"cmd\":\"clear_api_key_ack\",\"key\":\"%s\",\"ok\":true}", pwa_key);
        ble_mgr_notify_agent_sync(ack);
        return;
    }

    Serial.printf("[BLE/AGENT] cmd inconnue: %s\n", cmd);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[BOOT] Nestor Compagnon — demarrage");

    hal_pmu_init();        // Rails ALDO1/ALDO3 + IRQ bouton power
    hal_display_init();    // AMOLED QSPI + LVGL (reset pin 2)
    hal_touch_init();      // Attente 500 ms + CST9220 0x5A/0x1A
    hal_imu_init();        // QMI8658

    ui_status_bar_init();  // Barre d'état (toujours au-dessus)
    wifi_mgr_init();       // Portail captif "Compagnon_Setup"
    net_ota_init();        // ArduinoOTA (hostname: compagnon, port 3232)
    ble_mgr_init();        // BLE GATT : service GPS (push depuis téléphone)
    ble_mgr_set_wifi_prov_cb(ble_wifi_prov_cb);    // Provisioning WiFi depuis la PWA
    ble_mgr_set_agent_sync_cb(ble_agent_sync_cb);  // Commandes PWA (set_api_key, etc.)

    orchestrator_init();   // Planificateur + cerveau (stubs V1)
    ui_launcher_init();    // Carousel 4 apps → charge l'écran

    // Appui long bouton power → menu Veille / Arrêt complet
    hal_pmu_set_long_press_cb(ui_power_menu_show);

    Serial.println("[BOOT] Pret.");
}

void loop() {
    lv_timer_handler();    // LVGL — traitement des événements UI

    hal_pmu_tick();        // Lecture IRQ AXP2101 (short/long press)
    wifi_mgr_tick();       // Maintien connexion + portail captif
    net_ota_tick();        // Écoute OTA Arduino IDE
    ble_mgr_tick();        // BLE events (GPS depuis téléphone)

    ui_status_bar_tick();  // Mise à jour heure + batterie (toutes les 10 s)
    hal_imu_tick();        // Lecture orientation + détection changement
    if (hal_imu_changed()) {
        // Correspondance orientation physique → rotation logique LVGL
        // Rotation de base 270° (90° anti-horaire) pour l'orientation portrait,
        // cohérente avec lv_display_set_rotation(270) dans hal_display_init()
        static const lv_display_rotation_t rot_map[] = {
            LV_DISPLAY_ROTATION_270,  // ORIENT_PORTRAIT     (défaut boot)
            LV_DISPLAY_ROTATION_0,    // ORIENT_LANDSCAPE_L
            LV_DISPLAY_ROTATION_90,   // ORIENT_PORTRAIT_INV
            LV_DISPLAY_ROTATION_180,  // ORIENT_LANDSCAPE_R
        };
        lv_display_set_rotation(hal_display_get(), rot_map[hal_imu_orientation()]);
    }

    orchestrator_tick();      // Boucle cerveau / sync agents
    ui_launcher_btn_tick();   // Polling boutons hors timer LVGL (latence ~5 ms max)

    delay(1);   // 1ms → LVGL plus réactif (~1000 Hz max vs 200 Hz)
}
