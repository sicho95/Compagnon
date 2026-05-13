#pragma once
/**
 * net_utils.h — Helper HTTPS pour ESP32
 *
 * Utilise NetworkClientSecure alloué sur le heap (new/delete) pour éviter
 * la fragmentation mémoire et l'erreur SSL -32512 (Memory allocation failed).
 * setInsecure() désactive la vérification de certificat pour économiser ~8KB.
 */
#include <Arduino.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>

/**
 * Effectue un GET HTTPS.
 * @param url      URL complète (https://...)
 * @param out_body Corps de la réponse si code == 200
 * @param timeout_ms Timeout en ms (défaut 8000)
 * @return Code HTTP (200 = OK, -1 = erreur réseau)
 */
inline int https_get(const char* url, String& out_body, int timeout_ms = 8000) {
    NetworkClientSecure* client = new NetworkClientSecure();
    if (!client) return -1;
    client->setInsecure();  // Pas de vérif CA — économise ~8KB heap

    HTTPClient http;
    http.begin(*client, url);
    http.setTimeout(timeout_ms);

    int code = http.GET();
    if (code == 200) {
        out_body = http.getString();
    }
    http.end();
    delete client;  // Libération immédiate — critique pour éviter fragmentation
    return code;
}

/**
 * Effectue un POST HTTPS avec body JSON.
 * @param url         URL complète
 * @param payload     Corps de la requête
 * @param content_type Content-Type (défaut "application/json")
 * @param out_body    Réponse si code 2xx
 * @param timeout_ms  Timeout en ms
 * @return Code HTTP
 */
inline int https_post(const char* url, const String& payload,
                      const char* content_type, String& out_body,
                      int timeout_ms = 8000) {
    NetworkClientSecure* client = new NetworkClientSecure();
    if (!client) return -1;
    client->setInsecure();

    HTTPClient http;
    http.begin(*client, url);
    http.setTimeout(timeout_ms);
    http.addHeader("Content-Type", content_type);

    int code = http.POST(payload);
    if (code >= 200 && code < 300) {
        out_body = http.getString();
    }
    http.end();
    delete client;
    return code;
}
