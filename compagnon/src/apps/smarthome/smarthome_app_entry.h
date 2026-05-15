#pragma once
/**
 * smarthome_app_entry.h — Point d'entrée C pour le launcher
 *
 * Ce header expose smarthome_app_start() en linkage "C" afin d'éviter
 * tout conflit de name-mangling entre le .ino (compilé en C++) et les
 * modules C++ des apps.
 *
 * Usage dans launcher.cpp ou compagnon.ino :
 *   #include "src/apps/smarthome/smarthome_app_entry.h"
 *   smarthome_app_start();   // lance l'app + charge les creds NVS
 *
 * smarthome_app_start() est implémentée dans smarthome_app.cpp ;
 * elle appelle load_credentials() (NVS_KEY_TUYA_ID / NVS_KEY_TUYA_SEC)
 * avant de construire l'UI LVGL et de déclencher le premier fetch Tuya.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lance l'application Domotique (SmartHome / Tuya).
 *
 * - Lit les credentials Tuya depuis la NVS (clés configurées via BLE/PWA).
 * - Construit l'écran LVGL (grille 2 col, max 12 devices).
 * - Démarre une FreeRTOS task (core 0) pour le fetch HTTP Tuya.
 * - Rafraîchissement automatique toutes les 60 s via smarthome_app_tick().
 *
 * Si les clés Tuya sont absentes, l'app affiche le message
 * "Clés Tuya manquantes (PWA)" et attend qu'elles soient configurées.
 */
void smarthome_app_start(void);

/** Arrête proprement l'app et libère les objets LVGL. */
void smarthome_app_stop(void);

/** Retourne true si l'app est actuellement à l'écran. */
bool smarthome_app_is_running(void);

/**
 * À appeler dans loop() — déclenche le fetch Tuya toutes les 60 s
 * lorsque l'app est active.  Ne fait rien si l'app est fermée.
 */
void smarthome_app_tick(void);

/** Helpers credential (alternative à BLE) — écrivent aussi en NVS. */
void smarthome_set_tuya_id(const char *id);
void smarthome_set_tuya_secret(const char *secret);

#ifdef __cplusplus
}
#endif
