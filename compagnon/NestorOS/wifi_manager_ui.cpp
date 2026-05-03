/**
 * wifi_manager_ui.cpp
 * Gestion WiFi sans hardcoding, sans app tierce.
 *
 * Logique :
 *  1. Lecture credentials depuis NVS (Preferences).
 *  2. Si valides → connexion directe, fin.
 *  3. Sinon → SoftAP "Compagnon_Setup" + portail captif DNS + WebServer.
 *  4. L'écran affiche un QR code LVGL pour rejoindre Compagnon_Setup,
 *     + l'URL http://192.168.4.1 pour ouvrir manuellement.
 *  5. Le portail liste les réseaux voisins, l'utilisateur choisit
 *     et entre son mot de passe → stocké en NVS → reboot.
 *
 * Dépendances : WiFiManager (tzapu), Preferences (built-in ESP32),
 *               lv_qrcode (LV_USE_QRCODE doit être à 1 dans lv_conf.h).
 */
#include "wifi_manager_ui.h"
#include <WiFiManager.h>
#include <Preferences.h>
#include <lvgl.h>
#include <Arduino.h>
#include <WiFi.h>

// NTP après connexion
#define NTP_SERVER  "pool.ntp.org"
#define TZ_PARIS    "CET-1CEST,M3.5.0,M10.5.0/3"

static bool _connected   = false;
static lv_obj_t *scr_prov = NULL;

// ── Écran provisioning ──────────────────────────────────────────
static void show_provisioning_screen() {
  scr_prov = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_prov, lv_color_hex(0x06060F), 0);
  lv_scr_load(scr_prov);

  // Titre
  lv_obj_t *title = lv_label_create(scr_prov);
  lv_label_set_text(title, "Configuration WiFi");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x7EB8F7), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

  // Instructions
  lv_obj_t *ins = lv_label_create(scr_prov);
  lv_label_set_text(ins,
    "1. Scannez le QR code\n"
    "   OU connectez-vous a :\n"
    "   WiFi: Compagnon_Setup\n\n"
    "2. Une page s'ouvrira\n"
    "   sur votre telephone.\n\n"
    "3. Choisissez votre box\n"
    "   et entrez votre cle.");
  lv_obj_set_style_text_font(ins, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ins, lv_color_hex(0xA0A8C0), 0);
  lv_obj_set_style_text_align(ins, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_align(ins, LV_ALIGN_LEFT_MID, 20, 20);

  // QR code → "WIFI:T:WPA;S:Compagnon_Setup;P:;;" (réseau sans mot de passe)
  // Scannez avec l'appareil photo → connexion directe au portail.
#if LV_USE_QRCODE
  const char *qr_data = "WIFI:T:nopass;S:Compagnon_Setup;P:;;";
  lv_obj_t *qr = lv_qrcode_create(scr_prov);
  lv_qrcode_set_size(qr, 160);
  lv_qrcode_set_dark_color(qr, lv_color_hex(0x7EB8F7));
  lv_qrcode_set_light_color(qr, lv_color_hex(0x06060F));
  lv_qrcode_update(qr, qr_data, strlen(qr_data));
  lv_obj_align(qr, LV_ALIGN_RIGHT_MID, -20, 10);
#endif

  // URL fallback
  lv_obj_t *url = lv_label_create(scr_prov);
  lv_label_set_text(url, "http://192.168.4.1");
  lv_obj_set_style_text_font(url, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(url, lv_color_hex(0x6B75A0), 0);
  lv_obj_align(url, LV_ALIGN_BOTTOM_MID, 0, -20);
}

// ── Init ────────────────────────────────────────────────────────
void wifi_manager_init() {
  WiFiManager wm;
  wm.setHostname("Compagnon");
  wm.setConfigPortalTimeout(0);   // portail ouvert indéfiniment
  wm.setConnectTimeout(10);

  // Callback : portail captif ouvert → affiche l'écran LVGL
  wm.setAPCallback([](WiFiManager *wm) {
    Serial.println("[WiFi] Portail captif ouvert : Compagnon_Setup");
    show_provisioning_screen();
  });

  // Callback : connexion réussie → masque l'écran provisiong
  wm.setSaveConfigCallback([]() {
    Serial.println("[WiFi] Credentials sauvés → reboot");
    if (scr_prov) lv_obj_del(scr_prov);
  });

  // Nom du réseau SoftAP : Compagnon_Setup (sans mot de passe)
  bool ok = wm.autoConnect("Compagnon_Setup");

  if (ok) {
    _connected = true;
    Serial.printf("[WiFi] Connecté — IP: %s\n", WiFi.localIP().toString().c_str());
    // Sync NTP
    configTzTime(TZ_PARIS, NTP_SERVER);
  } else {
    Serial.println("[WiFi] Connexion échouée.");
  }
}

void wifi_manager_tick() {
  // WiFiManager gère son propre loop, rien à faire ici pour l'instant.
  // Réservé pour future logique de reconnexion.
}

bool wifi_manager_connected() {
  return _connected && WiFi.status() == WL_CONNECTED;
}
