#pragma once
/**
 * tuya_api.h — Client HTTPS Tuya OpenAPI (EU endpoint)
 *
 * Fonctionnement :
 *   1. tuya_api_init(client_id, client_secret) — stocker les credentials
 *   2. tuya_api_get_token()                    — récupère un access_token OAuth2
 *   3. tuya_api_get_devices(out, len)           — liste JSON des devices
 *   4. tuya_api_get_device_status(id, out, len) — état d'un device
 *   5. tuya_api_send_command(id, code, value)   — envoyer une commande
 *
 * Endpoint EU : openapi.tuyaeu.com
 * Token valide 7200 s, renouvelé automatiquement.
 */
#pragma once
#include <Arduino.h>

void tuya_api_init(const char *client_id, const char *client_secret);
bool tuya_api_get_token();
bool tuya_api_get_devices(char *out, size_t len);
bool tuya_api_get_device_status(const char *device_id, char *out, size_t len);
bool tuya_api_send_command(const char *device_id, const char *code, const char *value);
bool tuya_api_send_command_bool(const char *device_id, const char *code, bool value);
bool tuya_api_send_command_int(const char *device_id, const char *code, int value);
bool tuya_api_is_ready();
