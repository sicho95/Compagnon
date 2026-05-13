/**
 * net_utils.h — Helpers HTTPS GET/POST avec NetworkClientSecure alloué sur le heap
 *
 * Utilise setInsecure() pour éviter la gestion des certificats root CA.
 * NetworkClientSecure est alloué/libéré à chaque appel pour ne pas bloquer
 * la mémoire entre les requêtes.
 *
 * Usage basique :
 *   String body = https_get("https://api.example.com/data");
 *   String resp = https_post("https://api.example.com/cmd", "{\"key\":\"val\"}");
 *
 * Usage avec headers custom :
 *   int rc;
 *   String body = https_get_ex(url, "Authorization: Bearer TOKEN", &rc);
 */
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>

/**
 * https_get_ex — GET HTTPS avec headers custom optionnels.
 * @param url          URL complète (https://...)
 * @param extra_headers Headers supplémentaires au format "Key: Value\nKey2: Val2" (nullptr ok)
 * @param out_code     Si non nullptr, reçoit le code HTTP retourné
 * @return             Body de la réponse, ou String vide en cas d'erreur
 */
inline String https_get_ex(const char *url, const char *extra_headers, int *out_code) {
    NetworkClientSecure *client = new NetworkClientSecure;
    if (!client) { if (out_code) *out_code = -1; return ""; }
    client->setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(*client, url);
    if (extra_headers && extra_headers[0]) {
        // Parsing ligne par ligne "Key: Value\n..."
        String hdrs = extra_headers;
        int pos = 0;
        while (pos < (int)hdrs.length()) {
            int nl = hdrs.indexOf('\n', pos);
            String line = (nl < 0) ? hdrs.substring(pos) : hdrs.substring(pos, nl);
            int colon = line.indexOf(":");
            if (colon > 0)
                http.addHeader(line.substring(0, colon).c_str(),
                               line.substring(colon + 2).c_str());
            if (nl < 0) break;
            pos = nl + 1;
        }
    }
    int rc = http.GET();
    String result = (rc == 200) ? http.getString() : "";
    http.end();
    delete client;
    if (out_code) *out_code = rc;
    Serial.printf("[NET] GET %s → %d\n", url, rc);
    return result;
}

/**
 * https_post_ex — POST HTTPS avec body JSON et headers custom optionnels.
 */
inline String https_post_ex(const char *url, const char *body, const char *extra_headers, int *out_code) {
    NetworkClientSecure *client = new NetworkClientSecure;
    if (!client) { if (out_code) *out_code = -1; return ""; }
    client->setInsecure();
    HTTPClient http;
    http.setTimeout(8000);
    http.begin(*client, url);
    http.addHeader("Content-Type", "application/json");
    if (extra_headers && extra_headers[0]) {
        String hdrs = extra_headers;
        int pos = 0;
        while (pos < (int)hdrs.length()) {
            int nl = hdrs.indexOf('\n', pos);
            String line = (nl < 0) ? hdrs.substring(pos) : hdrs.substring(pos, nl);
            int colon = line.indexOf(":");
            if (colon > 0)
                http.addHeader(line.substring(0, colon).c_str(),
                               line.substring(colon + 2).c_str());
            if (nl < 0) break;
            pos = nl + 1;
        }
    }
    int rc = http.POST(body ? String(body) : "");
    String result = (rc > 0) ? http.getString() : "";
    http.end();
    delete client;
    if (out_code) *out_code = rc;
    Serial.printf("[NET] POST %s → %d\n", url, rc);
    return result;
}

// Versions simplifiées sans headers custom
inline String https_get(const char *url, int *out_code = nullptr) {
    return https_get_ex(url, nullptr, out_code);
}
inline String https_post(const char *url, const char *body, int *out_code = nullptr) {
    return https_post_ex(url, body, nullptr, out_code);
}
