#pragma once
/**
 * smarthome_app.h — Application domotique Tuya/SmartLife/Alexa
 *
 * Credentials Tuya stockés en NVS :
 *   NVS_KEY_TUYA_ID  ("tuya_client_id")  → Client ID iot.tuya.com
 *   NVS_KEY_TUYA_SEC ("tuya_client_sec") → Client Secret iot.tuya.com
 *
 * Fonctionnalités :
 *   - Liste des devices Tuya (lumières, prises)
 *   - Toggle ON/OFF par tap
 *   - Curseur luminosité (devices de type lumière)
 *   - Affichage capteurs temp/humidité en temps réel
 *   - Rafraîchissement auto toutes les 60 s
 */

void smarthome_app_start();
void smarthome_app_stop();
bool smarthome_app_is_running();   // retourne true si l'app est active
void smarthome_app_tick();

void smarthome_set_tuya_id(const char *id);
void smarthome_set_tuya_secret(const char *secret);
