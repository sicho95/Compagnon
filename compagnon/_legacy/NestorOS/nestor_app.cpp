/**
 * nestor_app.cpp
 * Lance la PWA Nestor dans un navigateur embarqué.
 *
 * Stratégie :
 *   1. Connexion WiFi (SSID/pass stockés en Preferences ou hardcodés)
 *   2. Affichage d'un QR code + URL sur l'écran de la montre
 *   3. Ouverture d'un mini-serveur HTTP local qui redirige vers la PWA
 *      hébergée sur GitHub Pages / Cloudflare Workers.
 *
 * L'ESP32-S3 n'a pas de navigateur natif — la PWA tourne sur le téléphone
 * du porteur, l'écran de la montre sert de remote control / status display.
 */
#include "nestor_app.h"
#include "bootloader_ui.h"
#include "orchestrator.h"
#include <lvgl.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Arduino.h>

// ── Config WiFi (modifiable via Preferences en production) ──────────────────
#define WIFI_SSID     "VotreSSID"
#define WIFI_PASS     "VotreMotDePasse"
#define PWA_URL       "https://sicho95.github.io/Nestor/"
#define CONNECT_TIMEOUT_MS 10000

// ── État ────────────────────────────────────────────────────────────────────
static lv_obj_t *scr_nestor   = NULL;
static lv_obj_t *lbl_status   = NULL;
static lv_obj_t *lbl_url      = NULL;
static lv_obj_t *spinner      = NULL;
static WebServer *server      = nullptr;
static bool       wifi_ok     = false;

// ── Callback retour ─────────────────────────────────────────────────────────
static void back_cb(lv_event_t *e) {
  nestor_app_stop();
  bootloader_ui_return();
}

// ── Met à jour le label de statut ────────────────────────────────────────────
static void set_status(const char *msg, uint32_t color) {
  if (!lbl_status) return;
  lv_label_set_text(lbl_status, msg);
  lv_obj_set_style_text_color(lbl_status, lv_color_hex(color), 0);
}

// ── Construction UI ─────────────────────────────────────────────────────────
static void build_ui() {
  scr_nestor = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_nestor, lv_color_hex(0x06060F), 0);

  // Bouton retour
  lv_obj_t *btn_back = lv_btn_create(scr_nestor);
  lv_obj_set_size(btn_back, 52, 36);
  lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_set_style_bg_color(btn_back, lv_color_hex(0x141830), 0);
  lv_obj_set_style_radius(btn_back, 10, 0);
  lv_obj_add_event_cb(btn_back, back_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_back = lv_label_create(btn_back);
  lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x7EB8F7), 0);
  lv_obj_center(lbl_back);

  // Titre
  lv_obj_t *title = lv_label_create(scr_nestor);
  lv_label_set_text(title, "Nestor");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x7EB8F7), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

  // Spinner pendant connexion
  spinner = lv_spinner_create(scr_nestor);
  lv_obj_set_size(spinner, 80, 80);
  lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -40);
  lv_obj_set_style_arc_color(spinner, lv_color_hex(0x7EB8F7), LV_PART_INDICATOR);

  // Label statut
  lbl_status = lv_label_create(scr_nestor);
  lv_label_set_text(lbl_status, "Connexion WiFi...");
  lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xE8EAF6), 0);
  lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 60);

  // Label URL (caché au départ)
  lbl_url = lv_label_create(scr_nestor);
  lv_label_set_text(lbl_url, "");
  lv_obj_set_style_text_font(lbl_url, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_url, lv_color_hex(0x6B75A0), 0);
  lv_obj_set_style_text_align(lbl_url, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(lbl_url, 420);
  lv_obj_align(lbl_url, LV_ALIGN_CENTER, 0, 100);

  lv_scr_load_anim(scr_nestor, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

// ── Tâche WiFi + serveur (core 0) ───────────────────────────────────────────
static void wifi_task(void *param) {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < CONNECT_TIMEOUT_MS) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_ok = true;
    String ip = WiFi.localIP().toString();
    Serial.printf("[NESTOR] WiFi OK — IP: %s\n", ip.c_str());

    // Serveur HTTP local de redirection vers la PWA
    server = new WebServer(80);
    server->on("/", []() {
      server->sendHeader("Location", PWA_URL, true);
      server->send(302, "text/plain", "");
    });
    server->begin();

    // Mise à jour UI depuis task (LVGL n'est pas thread-safe → on stocke et
    // l'update est faite dans le loop principal via flag)
    // Simplification : on appelle directement (single-core LVGL safe ici
    // car le loop LVGL tourne sur core 1 et on est sur core 0)
    lv_async_call([](void *arg) {
      if (!lbl_status || !lbl_url || !spinner) return;
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      char buf[64];
      snprintf(buf, sizeof(buf), "Ouvrez sur votre telephone :");
      set_status(buf, 0x4CAF50);
      lv_label_set_text(lbl_url, PWA_URL);
    }, NULL);

  } else {
    Serial.println("[NESTOR] WiFi ECHEC");
    lv_async_call([](void *arg) {
      if (!lbl_status || !spinner) return;
      lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
      set_status("WiFi non disponible\nVerifiez les parametres", 0xF44336);
    }, NULL);
  }

  vTaskDelete(NULL);
}

// ── API publique ─────────────────────────────────────────────────────────────
void nestor_app_start() {
  orchestrator_set_app(APP_NESTOR);
  wifi_ok = false;
  build_ui();
  // Lance la connexion WiFi en arrière-plan (core 0, 4KB stack)
  xTaskCreatePinnedToCore(wifi_task, "nestor_wifi", 4096, NULL, 1, NULL, 0);
}

void nestor_app_stop() {
  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }
  WiFi.disconnect(false);
  wifi_ok     = false;
  lbl_status  = NULL;
  lbl_url     = NULL;
  spinner     = NULL;
  orchestrator_set_app(APP_LAUNCHER);
}
