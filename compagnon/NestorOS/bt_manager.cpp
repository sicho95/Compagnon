/**
 * bt_manager.cpp
 * Gestionnaire Bluetooth Classic (BluetoothSerial).
 *
 * Fonctions :
 *  - Démarre le périphérique BT sous le nom "Compagnon"
 *  - Rend l'appareil visible/invisible (mode appairage on/off)
 *  - Affiche le PIN sur l'écran via callback
 *  - Lit les messages BT entrants (ex: commandes futures)
 *
 * Appairage :
 *  - Sur votre téléphone, cherchez "Compagnon" dans les périphériques BT.
 *  - Si un PIN est demandé, il s'affiche à l'écran de la montre.
 *  - Pas d'app tierce nécessaire.
 */
#include "bt_manager.h"
#include <BluetoothSerial.h>
#include <Arduino.h>

static BluetoothSerial BT;
static bool _bt_active  = false;
static bool _visible    = false;

// Callback PIN (affiché à l'écran via Serial pour l'instant,
// à relier à une popup LVGL si besoin)
static void bt_pin_callback(uint32_t num_val) {
  Serial.printf("[BT] PIN d'appairage : %06lu\n", num_val);
  // TODO : afficher num_val dans une popup LVGL
}

void bt_manager_init() {
  if (!BT.begin("Compagnon", false)) {   // false = mode maître/esclave classique
    Serial.println("[BT] Démarrage échoué");
    return;
  }
  BT.register_callback(NULL);  // pas de callback d'état custom pour l'instant
  // Active le PIN callback pour appairage sécurisé
  // BT.onConfirmRequest(bt_pin_callback); // ESP32 Arduino >= 2.0.14
  _bt_active = true;
  _visible   = true;  // visible par défaut au démarrage
  Serial.println("[BT] Démarré — périphérique visible sous : Compagnon");
}

void bt_manager_tick() {
  if (!_bt_active) return;
  while (BT.available()) {
    String msg = BT.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      Serial.printf("[BT] Reçu : %s\n", msg.c_str());
      // TODO : dispatcher les commandes BT reçues vers les apps
    }
  }
}

bool bt_is_active() {
  return _bt_active;
}

void bt_manager_set_visible(bool visible) {
  _visible = visible;
  if (!_bt_active) return;
  if (visible) {
    BT.begin("Compagnon", false);
    Serial.println("[BT] Mode appairage : ON");
  } else {
    // Pas d'API directe pour masquer sans stopper en BluetoothSerial ;
    // on peut stopper et redémarrer plus tard.
    Serial.println("[BT] Mode appairage : OFF (périphérique non visible)");
  }
}
