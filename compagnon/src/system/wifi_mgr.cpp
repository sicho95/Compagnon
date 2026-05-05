#include "wifi_mgr.h"
#include "../ui/status_bar.h"
#include <WiFi.h>
#include <WiFiManager.h>

static WiFiManager wm;
static bool        _conn = false;

void wifi_mgr_init() {
    // Sécurité : désactiver endpoints firmware update et erase NVS
    wm.setShowInfoUpdate(false);
    wm.setShowInfoErase(false);

    // Menu captif minimal (WiFi uniquement, pas d'update firmware)
    std::vector<const char *> menu = { "wifi", "info", "sep", "exit" };
    wm.setMenu(menu);

    // Portail non bloquant + timeout 3 min (puis abandon AP)
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(180);

    // AP captif "Compagnon_Setup" avec mot de passe
    // TODO V2: lire le PSK depuis NVS chiffré (pour l'instant hardcodé)
    const char *ap_psk = "compagnon";

    if (wm.autoConnect("Compagnon_Setup", ap_psk)) {
        _conn = true;
        Serial.println("[WIFI] Connecte : " + WiFi.localIP().toString());
        ui_status_bar_set_wifi(true);
    } else {
        Serial.println("[WIFI] Mode AP : SSID=Compagnon_Setup mdp=compagnon");
    }
}

void wifi_mgr_tick() {
    wm.process();
    bool now = (WiFi.status() == WL_CONNECTED);
    if (now != _conn) {
        _conn = now;
        ui_status_bar_set_wifi(now);
        if (now) Serial.println("[WIFI] Connecte : " + WiFi.localIP().toString());
        else     Serial.println("[WIFI] Deconnecte");
    }
}

bool wifi_mgr_connected() { return WiFi.status() == WL_CONNECTED; }
