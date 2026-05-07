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
#include "src/ui/status_bar.h"
#include "src/ui/launcher.h"

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
