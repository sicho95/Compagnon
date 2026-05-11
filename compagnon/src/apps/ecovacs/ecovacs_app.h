#pragma once
/**
 * ecovacs_app.h — Application robot aspirateur Ecovacs DEEBOT X8 Pro Omni
 *
 * Credentials stockés en NVS :
 *   NVS_KEY_ECOVACS_U ("ecovacs_user") → email du compte Ecovacs
 *   NVS_KEY_ECOVACS_P ("ecovacs_pass") → mot de passe Ecovacs
 *
 * Fonctionnalités :
 *   - Authentification cloud Ecovacs (eco-ng.ecovacs.com)
 *   - Affichage état robot : batterie, mode, statut
 *   - Commandes : Nettoyer / Arrêter / Retour base
 *   - Rafraîchissement état toutes les 30 s
 */
void ecovacs_app_start();
void ecovacs_app_stop();
void ecovacs_app_tick();
void ecovacs_set_credentials(const char *user, const char *pass);
