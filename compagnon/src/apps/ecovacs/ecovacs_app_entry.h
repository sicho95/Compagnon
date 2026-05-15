#pragma once
/**
 * ecovacs_app_entry.h — Point d'entrée C pour le launcher
 *
 * Ce header expose ecovacs_app_start() en linkage "C" afin d'éviter
 * tout conflit de name-mangling entre le .ino et les modules C++ des apps.
 *
 * Usage dans launcher.cpp ou compagnon.ino :
 *   #include "src/apps/ecovacs/ecovacs_app_entry.h"
 *   ecovacs_app_start();   // lance l'app + charge les creds NVS
 *
 * ecovacs_app_start() est implémentée dans ecovacs_app.cpp ;
 * elle appelle load_creds() (NVS_KEY_ECOVACS_U / NVS_KEY_ECOVACS_P)
 * avant de construire l'UI LVGL et de démarrer l'auth cloud Ecovacs.
 *
 * ⚠  Les FreeRTOS tasks réseau de l'app utilisent 24576 bytes de stack
 *    (mbedTLS TLS 1.2 nécessite ~16 KB minimum).
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lance l'application Ecovacs (robot aspirateur DEEBOT).
 *
 * - Lit email + mot de passe depuis la NVS (configurés via BLE/PWA).
 * - Construit l'écran LVGL (carte état + 3 boutons commande).
 * - Démarre une FreeRTOS task (core 0, stack 24576 B) pour
 *   l'authentification cloud Ecovacs puis la récupération d'état.
 * - Rafraîchissement automatique toutes les 30 s via ecovacs_app_tick().
 *
 * Si les credentials sont absents, l'app affiche "Identifiants manquants"
 * et attend qu'ils soient configurés via BLE (cmd set_api_key).
 */
void ecovacs_app_start(void);

/** Arrête proprement l'app et libère les objets LVGL. */
void ecovacs_app_stop(void);

/** Retourne true si l'app est actuellement à l'écran. */
bool ecovacs_app_is_running(void);

/**
 * À appeler dans loop() — déclenche le fetch Ecovacs toutes les 30 s
 * lorsque l'app est active.  Ne fait rien si l'app est fermée.
 */
void ecovacs_app_tick(void);

/** Helper credential (alternative à BLE) — écrit aussi en NVS. */
void ecovacs_set_credentials(const char *user, const char *pass);

#ifdef __cplusplus
}
#endif
